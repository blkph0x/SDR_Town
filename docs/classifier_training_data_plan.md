# Classifier Training Data Plan

Updated: 2026-06-11

## Decision

There is no single complete public dataset that covers the SDR Town target workflow:

- broadcast WFM
- NFM voice
- AM airband / AM utility
- USB / LSB / CW
- P25 / DMR / NXDN-style digital voice carriers
- DRM and other digital radio
- real receiver errors: overload, off-tune, multipath, adjacent channels, weak signals, and local noise

Public datasets are still valuable, but they are not enough by themselves. The state-of-the-art path for this app is:

1. **Bootstrap** on public RFML/modulation datasets.
2. **Generate** controlled synthetic data for missing labels and edge cases.
3. **Capture** our own real-world IQ/FFT/waterfall samples in SigMF.
4. **Train** a multi-input model offline.
5. **Ship** only models whose dataset licenses and validation results are acceptable.
6. **Keep deterministic DSP fallback** in the app at all times.

## Dataset Candidates

| Dataset / Source | Use | Strength | Limitation / Risk |
| --- | --- | --- | --- |
| DeepSig RadioML 2018.01A | Baseline modulation pretraining and benchmark | 24 analog/digital modulation types, HDF5 complex samples, widely used | CC BY-NC-SA 4.0. Treat as research/pretraining only unless release licensing is acceptable. Not protocol-complete for SDR Town. |
| DeepSig RadioML 2016.10A/B | Small baseline / smoke benchmark | Simple, common AMC benchmark | Synthetic, old, limited classes; not enough for production behavior. |
| TorchSig / Sig53 | Preferred synthetic generator and benchmark | MIT toolkit, 50+ / 53-class style generation, narrowband and wideband workflows, modern augmentation | Synthetic; still needs our real SDR captures for field performance. |
| RML22 | Research comparison | More realistic generation methodology than older RadioML | Verify license, availability, and class mapping before depending on it. |
| SigIDWiki / individual IQ examples | Sanity checks for P25/known signals | Useful examples for rare protocols | Not balanced training data; license/redistribution varies. Do not train shipped weights without permission. |
| SDRplay / vendor demo IQ files | Manual regression and app playback checks | Real off-air recordings | Not a labeled, balanced ML dataset. Use as validation examples only. |
| Our SDR Town SigMF corpus | Primary production dataset | Exact target hardware, bands, UI workflow, and local RF conditions | Must build capture/export/label tooling and keep data legally clean. |

## Licensing Rule

Do not bundle a model trained on a dataset until the dataset license is compatible with how we release SDR Town.

For now:

- DeepSig datasets are useful for experiments and reproducibility, but their NonCommercial/ShareAlike terms mean we should not casually ship derived weights.
- TorchSig is the cleanest public route for generated pretraining because the toolkit is open and configurable.
- Our own captures should be stored with explicit metadata and a project-controlled license.

## Class Taxonomy

The model should predict a signal family, not a decoder promise:

- `noise_or_unknown`
- `wfm_broadcast`
- `nfm_voice_12k5`
- `nfm_voice_25k`
- `am_voice_6k`
- `am_voice_10k`
- `am_voice_20k`
- `usb_voice`
- `lsb_voice`
- `cw`
- `p25_c4fm`
- `dmr_4fsk`
- `nxdn_4fsk`
- `drm_ofdm`
- `pager_pocsag`
- `packet_ax25`
- `other_digital_fsk`
- `wideband_ofdm_like`

The app then maps the family through a deterministic standards table:

- P25 C4FM -> exact 12.5 kHz channel, decoder-friendly filter recommendation, audio LPF off for symbol workflows.
- WFM broadcast -> exact 180 kHz, de-emphasis, 15 kHz audio LPF.
- AM wide -> 20 kHz, 9 kHz audio LPF.
- NFM 12.5 -> 12.5 kHz, 3 kHz audio LPF.
- Unknown -> estimated occupied bandwidth and conservative LPF.

## Model Shape

Use a multi-input, multi-task model:

Inputs:

- `waterfall_roi`: normalized `256 x 256 x 1` tile around the tuned frequency.
- `iq_window`: optional complex IQ segment as `N x 2`, likely 4096-16384 samples.
- `features`: SNR, estimated bandwidth, spectral flatness, symmetry, carrier dominance, band-plan prior, sample rate.

Backbone:

- Spectrogram branch: MobileNetV3-small, EfficientNet-lite, ConvNeXt-tiny, or a compact ViT/MobileViT.
- IQ branch: 1D ResNet/TCN or complex-conv style branch.
- Feature branch: small MLP.
- Fusion: concatenate embeddings, then task heads.

Heads:

- signal-present confidence
- mode/family logits
- bandwidth regression
- center-offset regression
- digital/analog flag
- confidence/calibration score

Runtime:

- Export to ONNX.
- App loads ONNX model if present.
- If model is absent, too slow, or low-confidence, keep using the deterministic classifier already implemented.

## Data Pipeline

1. Add a SigMF capture/export path in SDR Town:
   - IQ samples
   - FFT/waterfall tile
   - center frequency
   - tuned frequency
   - sample rate
   - hardware driver/model
   - RF gain/PPM
   - label
   - bandwidth/mode/filter settings
   - legal/use notes

2. Add offline Python tooling:
   - import RadioML HDF5/pickle
   - generate TorchSig samples
   - import SigMF captures
   - create normalized `256 x 256` tiles
   - create train/val/test splits by source, not random frame only

3. Train in phases:
   - Phase 1: TorchSig/RadioML bootstrap.
   - Phase 2: synthetic app-specific classes, especially P25/DMR/NXDN/DRM-like waveforms.
   - Phase 3: fine-tune on SDR Town SigMF captures.
   - Phase 4: field validation on unseen local captures.

4. Export:
   - ONNX float32 first.
   - Then ONNX Runtime dynamic quantization or INT8 calibration if CPU inference needs it.

## Acceptance Gates

Do not replace deterministic auto mode until these pass:

- WFM/AM/NFM/SSB/CW family accuracy: `>= 95%` on real holdout captures above usable SNR.
- P25/DMR/NXDN digital-family detection: `>= 90%` on real holdout captures.
- Unknown/noise false-positive rate: `< 2%`.
- Bandwidth regression median error: `< 15%`; standards table must still snap exact final values.
- Inference time: `< 20 ms` per ROI on a normal Windows CPU.
- Auto-mode stability: no repeated mode flapping on a stable signal for 5 minutes.
- All model/dataset licenses are recorded before release.

## Training Capture Implementation

The app now includes the first training capture foundation:

1. GUI: `Capture Training Sample` asks for an editable label and saves the current tuned signal.
2. CLI: `capture <label> [rx]` saves a sample from the selected receiver.
3. Output location: `%APPDATA%/SDR_Town/SDR Town/training_captures/`.
4. Each capture writes:
   - `.sigmf-data` with interleaved `cf32_le` IQ.
   - `.sigmf-meta` with SigMF core metadata plus SDR Town classifier/device/settings metadata.
   - `_tile.pgm` normalized waterfall ROI preview.
   - `_tile.f32` normalized float tile for training.
   - `manifest.jsonl` append-only index.

## Synthetic Bootstrap Data

Synthetic bootstrap generation is available:

```powershell
python scripts\generate_synthetic_classifier_data.py --count 20
python scripts\build_classifier_manifest.py
```

The generator writes the same `.sigmf-data`, `.sigmf-meta`, `_tile.pgm`, and `_tile.f32` artifacts as real captures. It covers bootstrap classes for unknown/noise, WFM, NFM 12.5/25, AM 20 kHz, USB, LSB, CW, P25-like C4FM, DMR-like 4FSK, and generic FSK.

Synthetic data is only a bootstrap. Real captures remain the production truth.

## Optional Model Backend

The C++ app now has a fail-closed optional model backend hook:

- CLI: `model status`
- CLI: `model load <path-to-model.onnx>`
- CLI: `model unload`

Normal builds do not require ONNX Runtime. The deterministic classifier remains active unless a future build enables a validated model runtime and successfully loads a model.

## Minimal Training Export

A minimal optional PyTorch training/export script is available:

```powershell
python -m pip install torch
python scripts\train_classifier.py --epochs 5
```

It trains a compact tile classifier from `train.jsonl`, `val.jsonl`, and `test.jsonl`, then exports:

- `sdr_town_classifier.onnx`
- `sdr_town_classifier.json` sidecar with label mapping and training history.

This is deliberately small and boring: it proves the end-to-end path before we spend time on larger CNN/ViT models.

## Immediate Next Build Step

Before adding more demods/decoders, collect enough real capture data and finalize runtime binding:

1. Capture real samples for WFM, NFM, AM, USB/LSB, CW, P25, DMR-like channels, weak/noisy signals, adjacent channel interference, and overload cases.
2. Use synthetic data only to balance classes that are underrepresented.
3. Train/export the first ONNX model and compare it against deterministic fallback on real holdout captures.
4. Finalize ONNX Runtime session binding in `ClassifierModelBackend`.
5. Keep the ONNX runtime optional until a trained model passes the acceptance gates.

Example:

```powershell
python scripts\build_classifier_manifest.py
python scripts\train_classifier.py --epochs 5
```

Outputs:

- `train.jsonl`
- `val.jsonl`
- `test.jsonl`
- `all.jsonl`
- `summary.json`
