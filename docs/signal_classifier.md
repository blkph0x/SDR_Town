# Signal Classifier Workflow

Updated: 2026-06-13

SDR Town now uses a hybrid classifier workflow:

1. **ROI extraction**
   - The classifier works around the tuned/monitor frequency, not the whole visible spectrum.
   - `WaterfallRoiBuilder` keeps recent FFT frames and can build a fixed-size normalized waterfall tile, defaulting to the model-friendly `256 x 256` shape.
   - Tile pixels are normalized from robust dB percentiles so the model/features are less sensitive to changing noise floors and waterfall contrast.

2. **Feature inference**
   - `AdvancedSignalClassifier` extracts bandwidth, SNR, noise floor, carrier dominance, sideband balance, spectral symmetry, spectral flatness, and temporal stability when a waterfall tile is available.
   - This is deterministic and fast today, so it runs without shipping a large trained model.
   - The interface is deliberately shaped like a future multi-task model output: signal class, confidence, estimated bandwidth, exact standard bandwidth, demod mode, filter type, audio LPF, and reason text.

3. **Post-processing lookup**
   - The classifier never asks a neural net to invent exact radio settings.
   - If the signal is classified as P25 C4FM/control-like, SDR Town maps it to exact 12.5 kHz RF bandwidth, NFM/C4FM handling, and a Root-Raised-Cosine style filter recommendation. Trunking control decode then decides whether a voice grant is Phase 1 FDMA or Phase 2 TDMA.
   - AM maps to 20 kHz RF bandwidth and 9 kHz audio LPF.
   - CW maps to the dedicated CW demodulator, 500 Hz to 1 kHz RF bandwidth, and a 700 Hz BFO audio tone.
   - WFM broadcast maps to 180 kHz RF bandwidth and 15 kHz audio LPF with WFM de-emphasis.
   - Unknown/weak signals fall back to the estimated occupied bandwidth with conservative low-pass filtering.

4. **Application integration**
   - Legacy `classifyMode()` and `classifyModeAround()` now call the advanced classifier.
   - GUI Auto BW/Auto mode combines the classifier with deterministic band-plan priors. HF/DX ranges below 30 MHz prefer known LF/MF/HF CW, SSB, medium-wave AM, and shortwave AM defaults unless the signal evidence is strong enough to override them.
   - The main window shows a live classifier status line with class, confidence, bandwidth, and filter recommendation.
   - CLI command `classify [dev] [rx]` prints the same decision and feature summary.
   - P25 voice follow reports the live decoder stage (`no voice sync`, `NID not validated`, `waiting voice frames`, `voice backend missing`, `Phase 2 metadata missing`, `Phase 2 TDMA framing/mask incomplete`, or `decoding clear voice`) in the GUI and CLI diagnostics.
   - GUI/CLI P25 monitoring mutes raw control-channel audio, includes an `Auto Follow Grants` checkbox in the GUI, and retunes back to the muted control channel after voice activity falls away.
   - Clear Phase 2 follow is gated until TDMA superframe timing, mask application, and MAC/ESS state are complete; burst detections remain diagnostic until that chain is validated on real captures.

## Why This Shape

This is the production path that scales to a true trained model later:

- The deterministic feature engine already improves AM/P25/WFM/NFM behavior now.
- The ROI tile builder gives us the exact input tensor shape needed for an ONNX/CNN/transformer model later.
- The settings lookup table keeps radio behavior exact and auditable.
- The classifier output includes confidence and reason text so bad decisions are visible during field testing.
- The training/data plan is tracked separately in `docs/classifier_training_data_plan.md`.
- Real training samples can now be captured from the GUI with `Capture Training Sample` or from CLI with `capture <label> [rx]`.
- Synthetic bootstrap samples can be generated with `scripts/generate_synthetic_classifier_data.py`.
- Train/validation/test manifests can be built with `scripts/build_classifier_manifest.py`.
- A compact optional PyTorch-to-ONNX training bridge is available at `scripts/train_classifier.py`.
- Optional model lifecycle is exposed in CLI with `model status`, `model load <path>`, and `model unload`.

## Next Classifier Work

1. Add hysteresis so AUTO changes only after stable evidence.
2. Add an optional ONNX Runtime backend:
   - Input: normalized `256 x 256` waterfall ROI.
   - Outputs: mode logits, bandwidth regression, signal-present confidence, optional digital-family logits.
3. Add captured-IQ training/export tooling:
   - Save ROI tiles and metadata from real signals.
   - Label P25, DMR, NFM voice, WFM broadcast, AM, USB/LSB, CW, and unknown.
4. Add decoder-aware digital classes:
   - P25 control/voice split.
   - DMR/NXDN/POCSAG/AX.25 candidate classes.
5. Persist classifier decisions with saved frequencies and scan hits for later review.
6. Keep hardening Phase 2 TDMA voice: derive/apply any required NAC/SYSID/WACN XOR mask with correct superframe/ISCH timing, decode TDMA MAC/encryption state earlier, and expand corpus-driven validation of AMBE frame mapping on real clear systems.
