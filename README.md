# Seeing Signals — RF Constellation Visualiser & Automatic Modulation Classification

An end-to-end pipeline for Automatic Modulation Classification (AMC), built for the
*Communication Systems II* assignment. A custom Qt6/C++ ground-station visualiser synthesises
16 digital modulation schemes at complex baseband, applies a configurable chain of physical
impairments (AWGN, Wiener/Ornstein–Uhlenbeck phase noise, I/Q imbalance, coherent CW
interference), and exports a labelled dataset of 3,072 constellation images. Two classifiers —
a multi-task ResNet-18 and a LoRA fine-tuned SmolVLM-256M — then predict the modulation and
impairment labels from the images.

**Pipeline:** bits → I/Q symbols → impaired constellation → 224×224 labelled image → classifier

## Repository layout
- **`Communications_Visualiser/`** — the Qt6/C++/QML application
  - `dspengine.{h,cpp}` — liquid-dsp modems, the impairment chain, dataset/SEP automation
  - `serialhandler.{h,cpp}` — live serial-port telemetry
  - `Main.qml`, `ConstellationPage.qml`, `DashboardPage.qml` — the UI
  - `CMakeLists.txt`
- **`ergasia_erevnas_template (1)/`** — the LaTeX report
  - `ergasia_erevnas_wcip.tex` — the report source (build with pdfLaTeX / Overleaf)
  - `figures/` — all report figures · `sample.bib` — references
- **`Telecomms_results/`** — the machine-learning part
  - `cnn_multitask.py` — multi-task ResNet-18 (5 labels, confusion matrices, acc-vs-SNR, generalization)
  - `VLM_MultiLabel.ipynb` / `vlm_multilabel.py` — SmolVLM multi-label fine-tune + leak-proof eval (Colab)
  - `VLM_Generalization.ipynb` / `vlm_generalization.py` — VLM leave-one-SNR-out generalization (Colab)
  - `Dataset_Pipeline.ipynb` — original single-task CNN notebook
  - `CNN_MultiTask_Results/`, `VLM_MultiLabel_Results/` — confusion matrices, per-task metrics
- `*.md` — design and analysis notes

## Building the app
**macOS** (Qt 6 + liquid-dsp + CMake):
```bash
brew install qt liquid-dsp cmake
cmake -S Communications_Visualiser -B build -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
```
**Windows:** build `liquid-dsp` under MSYS2/UCRT64, then build the app with the same toolchain
and deploy with `windeployqt6`. On GCC 14+ the liquid autotools need
`./configure ac_cv_lib_c_main=yes ac_cv_lib_m_main=yes CFLAGS="-O2 -fpermissive"`; only the
static `libliquid.a` is required (the shared-lib `-lc` failure is harmless).

## Running the ML
- **CNN:** `python Telecomms_results/cnn_multitask.py` (PyTorch with MPS/CUDA; dataset at `~/Desktop/RF_Dataset`).
- **VLM:** upload `VLM_MultiLabel.ipynb` and `VLM_Generalization.ipynb` to Google Colab (GPU),
  upload the dataset to Drive, **Run all**.

## Results (validation accuracy)
| Task | ResNet-18 | SmolVLM-256M |
|---|---|---|
| Modulation | 0.86 | 0.49 |
| Phase noise | 0.56 | 0.31 |
| I/Q imbalance | 0.80 | 0.26 |
| Interference | 0.92 | 0.32 |
| SNR range | 0.94 | 0.44 |
| **Mean** | **0.82** | **0.36** |

On the held-out 20 dB generalization test the CNN's modulation accuracy is maintained
(0.86 → 0.89) while the VLM collapses (0.49 → 0.26). Full analysis in the report.
