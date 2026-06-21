# vlm_generalization.py — VLM leave-one-SNR-out generalization (the "complete" method)
# Trains SmolVLM on SNR 10 & 30 dB, tests on the UNSEEN intermediate 20 dB,
# mirroring the CNN's generalization test. Run on Colab after Cell 1 (mount+extract).
import os, re
import pandas as pd
import torch
from datasets import Dataset, Features, Image as HFImage, Value
from transformers import (AutoProcessor, AutoModelForImageTextToText, TrainingArguments, Trainer)
from peft import LoraConfig, get_peft_model
from sklearn.metrics import accuracy_score
from tqdm.auto import tqdm

DRIVE   = "/content/drive/MyDrive/LLM_Google_collab"
IMG_DIR = "/content/dataset_images"
CSV     = f"{DRIVE}/dataset_labels.csv"
OUT     = f"{DRIVE}/Final_VLM_MultiLabel_Results"
os.makedirs(OUT, exist_ok=True)

FIELD = {"Modulation": "modulation", "PhaseNoise": "phase_noise", "IQ": "iq_imbalance",
         "Interference": "interference", "SNR": "snr_range"}
KEYS = list(FIELD.keys())
GEN_KEYS = ["Modulation", "PhaseNoise", "IQ", "Interference"]   # SNR excluded: 20 dB is unseen

df = pd.read_csv(CSV, keep_default_na=False)
CLASSES = {k: sorted(df[FIELD[k]].unique()) for k in KEYS}

SYSTEM_PROMPT = (
    "You are an expert RF engineer. Examine the constellation diagram and answer in EXACTLY "
    "this format, with no extra words:\n"
    "Modulation: <scheme>; PhaseNoise: <None|Low|Medium|High>; IQ: <None|Low|Medium|High>; "
    "Interference: <None|Low|Medium|High>; SNR: <Low|Medium|High>"
)
def answer_string(r):
    return (f"Modulation: {r['modulation']}; PhaseNoise: {r['phase_noise']}; "
            f"IQ: {r['iq_imbalance']}; Interference: {r['interference']}; SNR: {r['snr_range']}")
def format_row(r):
    return {"image": os.path.join(IMG_DIR, r["filename"]),
            "messages": [{"role": "user", "content": [{"type": "text", "text": SYSTEM_PROMPT}, {"type": "image"}]},
                         {"role": "assistant", "content": [{"type": "text", "text": answer_string(r)}]}]}

# --- LEAVE-ONE-SNR-OUT: train on 10 & 30 dB, test on the UNSEEN 20 dB ---
train_df = df[df["snr_range"].isin(["Low", "High"])]
test_df  = df[df["snr_range"] == "Medium"].sample(n=512, random_state=42)  # subsample for faster eval
print(f"train (10 & 30 dB): {len(train_df)}   test (UNSEEN 20 dB): {len(test_df)}")

features = Features({"image": HFImage(),
                    "messages": [{"role": Value("string"),
                                  "content": [{"type": Value("string"), "text": Value("string")}]}]})
hf_train = Dataset.from_list([format_row(r) for _, r in train_df.iterrows()], features=features)
hf_test  = Dataset.from_list([format_row(r) for _, r in test_df.iterrows()],  features=features)

model_id = "HuggingFaceTB/SmolVLM-256M-Instruct"
processor = AutoProcessor.from_pretrained(model_id)
model = AutoModelForImageTextToText.from_pretrained(model_id, torch_dtype=torch.bfloat16)
device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
model = model.to(device)
peft_model = get_peft_model(model, LoraConfig(r=16, lora_alpha=32, target_modules="all-linear",
                                              lora_dropout=0.05, bias="none", task_type="CAUSAL_LM"))
peft_model.print_trainable_parameters()

def collate_fn(examples):
    texts = [processor.apply_chat_template(e["messages"], tokenize=False) for e in examples]
    images = [e["image"] for e in examples]
    batch = processor(text=texts, images=images, return_tensors="pt", padding=True)
    labels = batch["input_ids"].clone()
    for i, e in enumerate(examples):
        ptxt = processor.apply_chat_template([e["messages"][0]], tokenize=False, add_generation_prompt=True)
        plen = len(processor.tokenizer(ptxt, return_tensors="pt")["input_ids"][0])
        labels[i, :plen] = -100
    labels[labels == processor.tokenizer.pad_token_id] = -100
    batch["labels"] = labels
    return batch

args = TrainingArguments(output_dir=f"{OUT}/smolvlm_generalize", per_device_train_batch_size=8,
    per_device_eval_batch_size=8, dataloader_pin_memory=True, dataloader_num_workers=2, bf16=True,
    num_train_epochs=3, learning_rate=5e-5, logging_steps=10, save_strategy="no",
    remove_unused_columns=False, push_to_hub=False, gradient_checkpointing=True,
    lr_scheduler_type="cosine", warmup_ratio=0.1)
Trainer(model=peft_model, args=args, train_dataset=hf_train, data_collator=collate_fn).train()

# --- leak-proof evaluation on the UNSEEN 20 dB ---
peft_model.eval()
FIELD_RE = {k: re.compile(rf"{k}\s*:\s*([^;\n]+)", re.IGNORECASE) for k in KEYS}
def canon(k, v):
    vl = v.strip().lower()
    for c in CLASSES[k]:
        if c.lower() == vl:
            return c
    return "Unknown"
def parse(t, strict=False):
    o = {}
    for k in KEYS:
        m = FIELD_RE[k].search(t); raw = m.group(1) if m else "Unknown"
        o[k] = raw.strip() if strict else canon(k, raw)
    return o

preds = {k: [] for k in KEYS}; trues = {k: [] for k in KEYS}; audit = []
for i in tqdm(range(len(hf_test))):
    s = hf_test[i]
    tf = parse(s["messages"][1]["content"][0]["text"], strict=True)
    prompt = processor.apply_chat_template([s["messages"][0]], tokenize=False, add_generation_prompt=True)
    inp = processor(text=prompt, images=s["image"], return_tensors="pt").to(device)
    with torch.no_grad():
        g = peft_model.generate(**inp, max_new_tokens=64, do_sample=False)
    raw = processor.decode(g[0][inp["input_ids"].shape[1]:], skip_special_tokens=True).strip()
    pf = parse(raw)
    for k in KEYS:
        trues[k].append(tf[k]); preds[k].append(pf[k])
    if i < 10:
        audit.append((s["messages"][1]["content"][0]["text"], raw))

with open(f"{OUT}/vlm_generalization.txt", "w") as f:
    f.write("VLM leave-one-SNR-out generalization: trained on 10 & 30 dB, tested on UNSEEN 20 dB.\n")
    f.write("(SNR task omitted: 20 dB is not a training class.)\n\n")
    for k in GEN_KEYS:
        a = accuracy_score(trues[k], preds[k]); f.write(f"{FIELD[k]}: {a:.4f}\n")
        print(f"unseen-20dB  {FIELD[k]:14s} acc = {a:.3f}")

print("\n--- sample raw generations on UNSEEN 20 dB ---")
for t, g in audit[:6]:
    print("TRUE:", t); print("GEN :", g); print()
print("Saved generalization results to:", OUT)
