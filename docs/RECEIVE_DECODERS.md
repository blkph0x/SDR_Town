# Receive Decoder Sessions

DEC-0085 adds a shared, internal C++ contract and immutable capability registry.
It is not a binary plugin ABI and does not load arbitrary third-party decoders.
Implemented entries are `rds`, `ctcss`, and `dcs`. SSTV, satellites and P25 are
not falsely advertised as adapters. SSTV/audio/image and satellite/IQ extensions
will be added with their first real consumers and independent fixtures.

## Input and Ownership

`ReceiveDecoderBlock` version 1 borrows a span of finite float samples for one
synchronous call. The caller must keep the span valid and immutable until the
call returns. The adapter never stores that span or creates a background task.
One DSP worker owns `process` and `reset`; `snapshot` is safe to poll from the GUI.
Destruction still requires the processing owner to have stopped, as with the
native decoders. The registry is immutable and safe to read concurrently.

Blocks carry the raw input domain, actual sample rate, target frequency,
source/device ID, epoch, absolute first-sample position and explicit gap flag.
Source ID must distinguish unrelated streams; device index is appropriate within
the current per-receiver GUI session. A source change forces reacquisition even
if the new stream reuses an epoch or cursor. Sample indices and rates define
sample time; there is no invented UTC origin or sample-loss detection upstream.

Raw MPX means the existing pre-audio WFM discriminator tap normalized to 75 kHz
deviation, not speaker audio. Raw FM discriminator is the existing NFM data tap
before speech filtering/de-emphasis/squelch; its normalization remains unchanged.
Passing the wrong domain is an error, not a request for implicit resampling.

| Adapter | Input | Rate | Samples/call | Runtime dependency |
|---|---|---|---|---|
| rds | raw-fm-multiplex | 128..384 kHz | <=262144 | sdrtown_rds_dsp.dll |
| ctcss | raw-fm-discriminator | 8..96 kHz | <=262144 | Native |
| dcs | raw-fm-discriminator | 8..96 kHz | <=262144 | Native |

These limits match existing backends. There is no new queue to overflow. An
oversized block is rejected; callers that subdivide data must preserve sample
positions and report actual loss. Later asynchronous backends need a separate
bounded ownership/backpressure contract; do not silently add queues here.

## Results and Failure

`DecoderProcessResult` distinguishes contract-version, domain, metadata/size and
backend failures. It does not mean a station/code was identified. Native payload
snapshots retain all existing confirmation gates, counters, reset semantics and
timestamps. The registry indicates a compiled adapter, not verified hardware,
available DLLs, RF conditions or completed live acceptance.

Wrong contract/domain/metadata clears native decode state. Non-finite samples
and backend failures are checked by the existing decoder. A failed process call
forces reacquisition next time. Missing/out-of-order samples, retunes, sample-rate
and epoch changes retain the backend's original reset behavior. Do not stitch
unrelated streams or keep displaying a previous source's confirmed identity.

The factory accepts only known IDs. It cannot select arbitrary paths or execute
catalogue-supplied commands. RDS retains its existing trusted-directory DLL load
and ABI checks. No third-party dependency has been added by this layer.

## Adoption and Diagnostics

GUI live RDS and CLI `rds mpx` now use the same factory/session. `rds bits` still
uses its appropriate already-demodulated protocol layer. Tone adapters are
available and tested, but existing GUI/CLI tone paths have deliberately not been
migrated in this step. P25 workers, vocoders, security and speaker queues are
unchanged. GUI self-test JSON includes `rdsContract` for path verification.

```powershell
build/bin/Release/SDR_Town.exe --cli --no-control-server --cmd decoders
python scripts/test_decoder_registry_cli.py
build/bin/Release/sdr_town_tests.exe '[decoder]'
python scripts/test_rds_cli.py
```

`decoders` reports JSON without probing radio hardware or loading optional DSP
modules. `backendProbe: not-performed` is intentional. Backend availability is
established by processing, not by the presence of a catalogue entry.

Parity tests compare the recorded MPX fixture against the unwrapped RDS decoder
after every input block, including three partitions and stream changes. All
station/group/correction/cursor counters are compared; independently measured
wall-clock timestamps are checked for presence rather than impossible equality.
The short fixture cannot prove PS/RT reception by itself; actual live RDS checks
remain necessary. DCS adapter partition parity and stale-tone rejection on
invalid/source-switched inputs are also covered.

## Live parity diagnostic

`python scripts/test_rds_live_gui.py --frequency-mhz 98.1 --expect-pi 0x2981 --expect-ps i98FM --parity --output build/rds_parity_new_run`
enables DEC-0086. The output directory must not already contain parity.jsonl.
SDR_TOWN_RDS_PARITY_LOG selects a JSONL output path; no diagnostic runs by default.
An independent native RDS instance consumes identical blocks and reset events.
The report is written on normal destruction, once per used receiver. Missing
reports fail the harness (a crash/unwritable path is not success). No samples
are retained. Decoder work approximately doubles; this is not a performance
benchmark. Station acquisition must pass separately. Use a unique path per process.

diagnose_rds_iq.py inspects cf32_le SigMF using NumPy/SoundFile, limited to ten
seconds / 3 MHz. It produces normalized MPX and relative spectral measurements
using an independent whole-record FFT channel filter. This noncausal filter is
NOT a production FIR replacement, calibrated SNR meter or proof of RDS presence.
test_rds_iq_diagnostic.py checks units/counts and malformed input.
