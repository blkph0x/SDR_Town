# SSTV live-path simulation (T-0085)

The hidden `[.sstv-virtual-sdr]` test registers a virtual Soapy device and uses
the production DeviceManager RX thread, IQ ring, independent receiver cursor,
SstvRfRouter, decodeSstvRfLive and streaming helper. It generates NFM at the
device's sample rate, paced against a monotonic clock. It does not transmit RF
or open physical SDR streams. Normal device discovery may enumerate installed
drivers. Settings use Qt's test namespace, not the user's normal settings.

This is more than a fast RF-router test, but it is not an antenna/USB-driver
test or a test of the GUI controls that select the receiver. No production
DeviceManager, receiver, DSP or P25 code was modified.

Build `sdr_town_tests` with SoapySDR and the SSTV helper enabled. Use a separate
Python test environment with numpy, scipy and soundfile (existing RF-test deps):

```text
python scripts/test_sstv_virtual_sdr.py recording.wav --out build/sstv-simulation
python scripts/test_sstv_virtual_sdr.py recording.wav --out build/sstv-late --skip-seconds 5 --expected-images 0
python scripts/test_sstv_virtual_sdr.py recording.wav --out build/sstv-late-nfm --skip-seconds 5 --rf-mode NFM --allow-partial
```

Second command assumes the skipped portion contains the entire VIS header and
no later header exists; it is an explicit negative fixture, not a general rule.
`--rf-mode NFM` bypasses RF Auto's header-based selection, not image decoding.
Output directories must be new. Inputs must be <=120 seconds. Raw PCM, detailed
simulation log, image and final JSON report remain local; never commit a user's
recording or generated pixels. No telemetry submission is performed by this test.

## Observed baseline

User-provided 111.343333-second mono/44100-Hz Scottie1 recording: fast file and
synthetic NFM routes both decoded 320x256. Paced virtual device at 2.4 MS/s emitted
269624000 IQ samples (includes one second fixture lead-in); Auto selected NFM;
33 previews and a complete 256-row image. Output audio 5310489 samples at 48 kHz.
Mean absolute RGB difference from file reference: 0.64425/255. Image inspected.
This proves the simulated live path works for this fixture, not that the user's
off-air failure is fixed. Obtain real IQ including the transmission beginning
to distinguish signal quality, tuning, missing header or device discontinuity.

Skipping the first five seconds reproduces the symptom with RF Auto: 106.34
seconds of recorded signal are processed but route remains searching, zero audio
samples reach the helper and no image is produced. This isolates the header gate
for that controlled negative case, not the cause of an unseen off-air capture.
The identical late-entry recording with manual NFM and image Auto recovers one
248/256-row Scottie1 partial image (5152481 helper input samples). Missing rows
are not fabricated. Starting before the header is required for the full picture.
