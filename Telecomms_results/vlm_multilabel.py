# vlm_multilabel.py — SmolVLM-256M multi-label fine-tune + leak-proof evaluation
# ---------------------------------------------------------------------------
# Run on Colab (GPU). FIRST run your original Cell 1 that mounts Drive and
# extracts dataset_images.zip to /content/dataset_images, then paste this whole
# file into the next cell and run it.
#
# IMPORTANT: upload the *v2* dataset to Drive so the VLM trains on the SAME data
# as the CNN (true hexagonal HQAM + corrected noise):
#   - LLM_Google_collab/dataset_labels.csv      (from ~/Desktop/RF_Dataset)
#   - LLM_Google_collab/dataset_images.zip      (zip of ~/Desktop/RF_Dataset/*.png)
# ---------------------------------------------------------------------------
import os, re
import pandas as pd
import torch
from datasets import Dataset, Features, Image as HFImage, Value
from transformers import (AutoProcessor, AutoModelForImageTextToText,
                          TrainingArguments, Trainer)
from peft import LoraConfig, get_peft_model
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from sklearn.metrics import (confusion_matrix, ConfusionMatrixDisplay,
                             classification_report, accuracy_score)
from tqdm.auto import tqdm

# ----------------------------------------------------------------- paths
DRIVE   = "/content/drive/MyDrive/LLM_Google_collab"
IMG_DIR = "/content/dataset_images"
CSV     = f"{DRIVE}/dataset_labels.csv"
OUT     = f"{DRIVE}/Final_VLM_MultiLabel_Results"
os.makedirs(OUT, exist_ok=True)

# answer-field key  <->  dataframe column
FIELD = {"Modulation": "modulation", "PhaseNoise": "phase_noise", "IQ": "iq_imbalance",
         "Interference": "interference", "SNR": "snr_range"}
KEYS = list(FIELD.keys())

df = pd.read_csv(CSV, keep_default_na=False)          # keep literal "None"
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
            "messages": [
                {"role": "user", "content": [{"type": "text", "text": SYSTEM_PROMPT}, {"type": "image"}]},
                {"role": "assistant", "content": [{"type": "text", "text": answer_string(r)}]}]}

features = Features({"image": HFImage(),
                    "messages": [{"role": Value("string"),
                                  "content": [{"type": Value("string"), "text": Value("string")}]}]})
hf = Dataset.from_list([format_row(r) for _, r in df.iterrows()], features=features)
hf = hf.train_test_split(test_size=0.20, seed=42)
print("train/val:", len(hf["train"]), len(hf["test"]))

# ----------------------------------------------------------------- model + LoRA
model_id = "HuggingFaceTB/SmolVLM-256M-Instruct"
processor = AutoProcessor.from_pretrained(model_id)
model = AutoModelForImageTextToText.from_pretrained(model_id, torch_dtype=torch.bfloat16)
device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
model = model.to(device)
peft_model = get_peft_model(model, LoraConfig(
    r=16, lora_alpha=32, target_modules="all-linear",
    lora_dropout=0.05, bias="none", task_type="CAUSAL_LM"))
peft_model.print_trainable_parameters()

def collate_fn(examples):
    texts = [processor.apply_chat_template(e["messages"], tokenize=False) for e in examples]
    images = [e["image"] for e in examples]
    batch = processor(text=texts, images=images, return_tensors="pt", padding=True)
    labels = batch["input_ids"].clone()
    for i, e in enumerate(examples):                 # mask the prompt tokens
        ptxt = processor.apply_chat_template([e["messages"][0]], tokenize=False, add_generation_prompt=True)
        plen = len(processor.tokenizer(ptxt, return_tensors="pt")["input_ids"][0])
        labels[i, :plen] = -100
    labels[labels == processor.tokenizer.pad_token_id] = -100
    batch["labels"] = labels
    return batch

args = TrainingArguments(
    output_dir=f"{OUT}/smolvlm_multilabel", per_device_train_batch_size=8,
    per_device_eval_batch_size=8, dataloader_pin_memory=True, dataloader_num_workers=2,
    bf16=True, num_train_epochs=3, learning_rate=5e-5, logging_steps=10,
    save_strategy="epoch", eval_strategy="epoch", remove_unused_columns=False,
    push_to_hub=False, gradient_checkpointing=True, lr_scheduler_type="cosine", warmup_ratio=0.1)
Trainer(model=peft_model, args=args, train_dataset=hf["train"],
        eval_dataset=hf["test"], data_collator=collate_fn).train()

# ----------------------------------------------------------------- LEAK-PROOF EVALUATION
peft_model.eval()
FIELD_RE = {k: re.compile(rf"{k}\s*:\s*([^;\n]+)", re.IGNORECASE) for k in KEYS}

def canon(k, value):
    """Map an extracted value to its canonical class, else 'Unknown' (counts as wrong)."""
    v = value.strip().lower()
    for c in CLASSES[k]:
        if c.lower() == v:
            return c
    return "Unknown"

def parse(text, strict_true=False):
    out = {}
    for k in KEYS:
        m = FIELD_RE[k].search(text)
        raw = m.group(1) if m else "Unknown"
        out[k] = raw.strip() if strict_true else canon(k, raw)
    return out

preds = {k: [] for k in KEYS}
trues = {k: [] for k in KEYS}
audit = []

for i in tqdm(range(len(hf["test"]))):
    s = hf["test"][i]
    true_text = s["messages"][1]["content"][0]["text"]
    tf = parse(true_text, strict_true=True)                  # ground truth (exact)
    # feed ONLY the user prompt — the assistant answer is never in the input
    prompt = processor.apply_chat_template([s["messages"][0]], tokenize=False, add_generation_prompt=True)
    inputs = processor(text=prompt, images=s["image"], return_tensors="pt").to(device)
    with torch.no_grad():
        gen = peft_model.generate(**inputs, max_new_tokens=64, do_sample=False)
    raw = processor.decode(gen[0][inputs["input_ids"].shape[1]:], skip_special_tokens=True).strip()
    pf = parse(raw)                                          # prediction (strict canon)
    for k in KEYS:
        trues[k].append(tf[k]); preds[k].append(pf[k])
    if i < 25:
        audit.append({"true": true_text, "generated": raw})

# save raw generations so the result can be audited by eye
pd.DataFrame(audit).to_csv(f"{OUT}/vlm_raw_generations_sample.csv", index=False)
pd.DataFrame({**{f"true_{k}": trues[k] for k in KEYS},
              **{f"pred_{k}": preds[k] for k in KEYS}}).to_csv(
    f"{OUT}/vlm_predictions_multilabel.csv", index=False)

# per-task confusion matrices, reports, accuracy
with open(f"{OUT}/vlm_pertask_accuracy.txt", "w") as f:
    for k in KEYS:
        col = FIELD[k]
        labs = CLASSES[k] + (["Unknown"] if "Unknown" in preds[k] else [])
        cm = confusion_matrix(trues[k], preds[k], labels=labs)
        fig, ax = plt.subplots(figsize=(8, 8) if k == "Modulation" else (5, 5))
        ConfusionMatrixDisplay(cm, display_labels=labs).plot(
            ax=ax, cmap="Oranges", xticks_rotation="vertical", colorbar=False)
        ax.set_title(f"VLM — {col} (validation)")
        fig.tight_layout(); fig.savefig(f"{OUT}/vlm_cm_{col}.pdf"); plt.close(fig)
        open(f"{OUT}/vlm_report_{col}.txt", "w").write(
            classification_report(trues[k], preds[k], zero_division=0))
        a = accuracy_score(trues[k], preds[k])
        f.write(f"{col}: {a:.4f}\n"); print(f"{col:14s} acc = {a:.3f}")

# accuracy vs SNR
snr_db = {"Low": 10, "Medium": 20, "High": 30}
fig, ax = plt.subplots(figsize=(7, 5))
for k in KEYS:
    xs, ys = [], []
    for s in ["Low", "Medium", "High"]:
        idx = [j for j in range(len(trues["SNR"])) if trues["SNR"][j] == s]
        if idx:
            xs.append(snr_db[s]); ys.append(sum(preds[k][j] == trues[k][j] for j in idx) / len(idx))
    ax.plot(xs, ys, "o-", label=FIELD[k])
ax.set_xlabel("SNR (Es/N0, dB)"); ax.set_ylabel("Validation accuracy"); ax.set_ylim(0, 1.02)
ax.set_xticks([10, 20, 30]); ax.grid(alpha=0.3); ax.legend(fontsize=8)
ax.set_title("VLM accuracy vs SNR (per task)")
fig.tight_layout(); fig.savefig(f"{OUT}/vlm_accuracy_vs_snr.pdf"); plt.close(fig)

print("\n--- sample raw generations (verify it is NOT cheating) ---")
for a in audit[:8]:
    print("TRUE:", a["true"]); print("GEN :", a["generated"]); print()
print("Saved all VLM artifacts to:", OUT)
