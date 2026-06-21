#!/usr/bin/env python3
"""
Multi-task ResNet-18 for RF constellation classification.

Predicts ALL structured labels required by the assignment:
    modulation, phase_noise, iq_imbalance, interference, snr_range
and produces, for the report:
    - per-task confusion matrices  (cnn_cm_<task>.pdf)
    - per-task classification reports (cnn_report_<task>.txt)
    - per-task accuracy summary     (cnn_pertask_accuracy.txt)
    - accuracy as a function of SNR (cnn_accuracy_vs_snr.pdf)
    - a leave-one-SNR-out generalization test (cnn_generalization.txt)

Run it in the SAME PyTorch environment that ran Dataset_Pipeline.ipynb:
    python cnn_multitask.py
Expects the dataset at ~/Desktop/RF_Dataset (images + dataset_labels.csv).
"""
import os, copy
import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from torchvision import transforms, models
from PIL import Image
from sklearn.model_selection import train_test_split
from sklearn.metrics import (confusion_matrix, ConfusionMatrixDisplay,
                             classification_report, accuracy_score)
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ----------------------------------------------------------------- configuration
DATA_DIR = os.path.expanduser("~/Desktop/RF_Dataset")
CSV      = os.path.join(DATA_DIR, "dataset_labels.csv")
OUT      = os.path.expanduser("~/Desktop/Aristurtle Dashboard/Comms_visualiser/"
                              "Telecomms_results/CNN_MultiTask_Results")
os.makedirs(OUT, exist_ok=True)
TASKS  = ["modulation", "phase_noise", "iq_imbalance", "interference", "snr_range"]
EPOCHS = 12

device = torch.device("mps" if torch.backends.mps.is_available()
                      else ("cuda" if torch.cuda.is_available() else "cpu"))
print("Device:", device)

df = pd.read_csv(CSV, keep_default_na=False)  # keep the literal string "None" as a label
LMAP = {t: {v: i for i, v in enumerate(sorted(df[t].unique()))} for t in TASKS}
LINV = {t: {i: v for v, i in LMAP[t].items()} for t in TASKS}
NCLS = {t: len(LMAP[t]) for t in TASKS}
print("Classes per task:", NCLS)

TF = transforms.Compose([
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
])

class MultiTaskDS(Dataset):
    def __init__(self, frame):
        self.f = frame.reset_index(drop=True)
    def __len__(self):
        return len(self.f)
    def __getitem__(self, i):
        row = self.f.iloc[i]
        img = TF(Image.open(os.path.join(DATA_DIR, row["filename"])).convert("RGB"))
        y = {t: torch.tensor(LMAP[t][row[t]], dtype=torch.long) for t in TASKS}
        return img, y

def collate(batch):
    imgs = torch.stack([b[0] for b in batch])
    ys = {t: torch.stack([b[1][t] for b in batch]) for t in TASKS}
    return imgs, ys

class MultiHeadResNet(nn.Module):
    """One shared ResNet-18 backbone, one linear head per task."""
    def __init__(self):
        super().__init__()
        base = models.resnet18(weights=models.ResNet18_Weights.DEFAULT)
        d = base.fc.in_features
        base.fc = nn.Identity()
        self.backbone = base
        self.heads = nn.ModuleDict({t: nn.Linear(d, NCLS[t]) for t in TASKS})
    def forward(self, x):
        z = self.backbone(x)
        return {t: self.heads[t](z) for t in TASKS}

crit = nn.CrossEntropyLoss()

def train_model(train_df, val_df, epochs=EPOCHS, tag="main"):
    tr = DataLoader(MultiTaskDS(train_df), batch_size=32, shuffle=True,  num_workers=0, collate_fn=collate)
    va = DataLoader(MultiTaskDS(val_df),   batch_size=32, shuffle=False, num_workers=0, collate_fn=collate)
    model = MultiHeadResNet().to(device)
    opt = optim.Adam(model.parameters(), lr=1e-3)
    best_avg, best_wts = 0.0, copy.deepcopy(model.state_dict())
    for ep in range(epochs):
        model.train()
        for x, y in tr:
            x = x.to(device); y = {t: y[t].to(device) for t in TASKS}
            opt.zero_grad()
            out = model(x)
            loss = sum(crit(out[t], y[t]) for t in TASKS)
            loss.backward(); opt.step()
        model.eval(); correct = {t: 0 for t in TASKS}; n = 0
        with torch.no_grad():
            for x, y in va:
                x = x.to(device); out = model(x); n += x.size(0)
                for t in TASKS:
                    correct[t] += (out[t].argmax(1).cpu() == y[t]).sum().item()
        accs = {t: correct[t] / n for t in TASKS}
        avg = sum(accs.values()) / len(TASKS)
        print(f"[{tag}] epoch {ep+1:02d}/{epochs}  avg={avg:.3f}  " +
              "  ".join(f"{t[:4]}={accs[t]:.2f}" for t in TASKS))
        if avg > best_avg:
            best_avg, best_wts = avg, copy.deepcopy(model.state_dict())
    model.load_state_dict(best_wts)
    return model

def evaluate(model, val_df):
    va = DataLoader(MultiTaskDS(val_df), batch_size=32, shuffle=False, num_workers=0, collate_fn=collate)
    model.eval()
    P = {t: [] for t in TASKS}; Y = {t: [] for t in TASKS}
    with torch.no_grad():
        for x, y in va:
            x = x.to(device); out = model(x)
            for t in TASKS:
                P[t].extend(out[t].argmax(1).cpu().numpy().tolist())
                Y[t].extend(y[t].numpy().tolist())
    return P, Y

# ----------------------------------------------------------- 1) main multi-task model
train_df, val_df = train_test_split(df, test_size=0.20, random_state=42,
                                    stratify=df["modulation"])
print(f"\n=== Multi-task training: {len(train_df)} train / {len(val_df)} val ===")
model = train_model(train_df, val_df, tag="multitask")
P, Y = evaluate(model, val_df)

print("\n--- Per-task validation accuracy ---")
with open(os.path.join(OUT, "cnn_pertask_accuracy.txt"), "w") as f:
    for t in TASKS:
        labels = [LINV[t][i] for i in range(NCLS[t])]
        cm = confusion_matrix(Y[t], P[t], labels=list(range(NCLS[t])))
        fig, ax = plt.subplots(figsize=(8, 8) if t == "modulation" else (5, 5))
        ConfusionMatrixDisplay(cm, display_labels=labels).plot(
            ax=ax, cmap="Blues", xticks_rotation="vertical", colorbar=False)
        ax.set_title(f"CNN — {t} (validation)")
        fig.tight_layout(); fig.savefig(os.path.join(OUT, f"cnn_cm_{t}.pdf")); plt.close(fig)
        with open(os.path.join(OUT, f"cnn_report_{t}.txt"), "w") as rf:
            rf.write(classification_report(Y[t], P[t], target_names=labels, zero_division=0))
        acc = accuracy_score(Y[t], P[t])
        print(f"  {t:14s} acc = {acc:.3f}")
        f.write(f"{t}: {acc:.4f}\n")

# ----------------------------------------------------------- 2) accuracy vs SNR
snr_order = ["Low", "Medium", "High"]; snr_db = {"Low": 10, "Medium": 20, "High": 30}
snr_true = [LINV["snr_range"][i] for i in Y["snr_range"]]
fig, ax = plt.subplots(figsize=(7, 5))
for t in TASKS:
    xs, ys = [], []
    for s in snr_order:
        idx = [k for k in range(len(snr_true)) if snr_true[k] == s]
        if idx:
            xs.append(snr_db[s]); ys.append(np.mean([P[t][k] == Y[t][k] for k in idx]))
    ax.plot(xs, ys, "o-", label=t)
ax.set_xlabel("SNR (Es/N0, dB)"); ax.set_ylabel("Validation accuracy")
ax.set_title("CNN accuracy vs SNR (per task)"); ax.set_ylim(0, 1.02)
ax.set_xticks([10, 20, 30]); ax.grid(alpha=0.3); ax.legend(fontsize=8)
fig.tight_layout(); fig.savefig(os.path.join(OUT, "cnn_accuracy_vs_snr.pdf")); plt.close(fig)
print("Saved cnn_accuracy_vs_snr.pdf")

# ----------------------------------------------------------- 3) generalization (unseen SNR)
# Train on SNR Low+High (10 & 30 dB); test on the UNSEEN intermediate 20 dB.
# (snr_range itself is excluded: a softmax head can't predict a class it never trained on.)
gen_train = df[df["snr_range"].isin(["Low", "High"])]
gen_test  = df[df["snr_range"] == "Medium"]
print(f"\n=== Generalization: train 10&30 dB, test UNSEEN 20 dB "
      f"({len(gen_train)} / {len(gen_test)}) ===")
gmodel = train_model(gen_train, gen_test, tag="generalize")
GP, GY = evaluate(gmodel, gen_test)
with open(os.path.join(OUT, "cnn_generalization.txt"), "w") as f:
    f.write("Leave-one-SNR-out generalization: trained on 10 & 30 dB, "
            "tested on UNSEEN intermediate 20 dB.\n")
    f.write("(snr_range task omitted; 20 dB is not a training class.)\n\n")
    for t in [x for x in TASKS if x != "snr_range"]:
        a = accuracy_score(GY[t], GP[t])
        f.write(f"{t}: {a:.4f}\n")
        print(f"  unseen-20dB  {t:14s} acc = {a:.3f}")

print("\nALL DONE. Artifacts saved to:\n ", OUT)
