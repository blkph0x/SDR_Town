# Inmarsat IQ Testing

This harness tests input and physical diagnostics, **not Aero voice decoding**.
No receiver is opened or retuned by replay; P25 is unchanged.

## Formats

SigMF metadata/data pairs follow https://sigmf.org/ v1.2.6. Supported datatypes:
`cf32_le/be`, `cf64_le/be`, `ci16_le/be`, `ci32_le/be`, `cu16_le/be`,
`cu32_le/be`, `ci8`, `cu8` (14 formats). I precedes Q. Floating samples must be
finite. One complex channel only; every capture segment must include its center
frequency and the first starts at zero. Retune segments reset the decoder. Metadata
over 1 MiB, required extensions, per-segment byte headers, byte offsets, trailers
and global-index gaps are rejected, not silently ignored.

RIFF WAV: stereo PCM16 or IEEE float32, left=I/right=Q. Set the actual RF center
explicitly; WAV sample rate is used. Demodulated mono JAERO audio is not IQ.
RF64, WAV extensible/compressed/24-bit, archives and proprietary containers are
not supported yet. Raw files support all 14 types but require sample rate/center.
No filename guessing. Input rate 8 kHz..40 MHz is a resource contract, not RF
qualification. Reads are at most 65536 samples and never cross a retune boundary.

## GUI

Tools > Inmarsat Aero > Open IQ replay. Choose a file and format. For raw/WAV
enter the actual center; for raw also enter the sample rate. Select the physical
probe and, optionally, a channel within the captured bandwidth. Channel zero uses
capture center. Pause/resume preserve DSP history; seeking resets it. Real-time
pacing never drops input to catch up; fast mode changes pacing only. EOF stops;
Play starts a new session. Logs display the exact session/report path.

## Automation

```powershell
.\SDR_Town.exe --cli --inmarsat-iq "C:\captures\aero.sigmf-meta" --inmarsat-mode 10500 --inmarsat-fast --inmarsat-result "C:\captures\result.json"
.\SDR_Town.exe --inmarsat-iq "C:\captures\aero.wav" --inmarsat-center 1542935000 --inmarsat-mode 8400 --inmarsat-exit-complete --inmarsat-result "C:\captures\gui-result.json"
.\SDR_Town.exe --cli --inmarsat-iq "C:\captures\raw.iq" --inmarsat-format ci16_le --inmarsat-rate 2048000 --inmarsat-center 1542935000 --inmarsat-channel 1542940000 --inmarsat-mode 1200
```

CLI frequencies/rates are **Hz**. Modes: `600`, `1200`, `10500`, `8400`, `egc`.
Optional `--inmarsat-log-dir` selects the report folder. `--inmarsat-replay` alone
opens the isolated GUI. `--inmarsat-exit-complete` exits GUI automation at EOF/error;
CLI always exits. Exit 0 means completed file processing, NOT intelligible voice.
Exit 2 means input/report failure.

`scripts/verify_inmarsat_replay.py --exe ... --out ...` checks synthetic GUI/CLI,
paced/fast parity plus truncated input. Reporting is off unless `--remote` is
explicitly supplied to use the existing app configuration.

## Diagnostics And Consent

Local JSONL logs: `%APPDATA%\SDR_Town\SDR Town\inmarsat_diagnostics`. They include
the selected file path, RF center/channel, sample position, gaps/resets, input
RMS/peak, processing times, physical counters and unavailable-capability flags.
No raw IQ or PCM. Limit: 8 MiB per session plus final summary. Existing logs are
not deleted. `protocolDecoderAvailable=false`, `aeroVocoderAvailable=false`,
`pcmSamples=0` are expected today, even for a good Aero recording.

Tester packages point at `https://gearsqueens.online/sdr-town-diag/ingest` with
reporting disabled. The replay sharing checkbox opts in for the current run after
disclosure. Existing enabled local configuration remains respected. CLI opt-in:
`--diag-url https://gearsqueens.online/sdr-town-diag/ingest`. Force reporting off:
`--no-remote-diagnostics` (also disables the sharing checkbox).

Remote Inmarsat fields are allowlisted numerical counters, rate, mode, timings,
quality, state/error category and session ID. No paths, exact RF frequencies, IQ,
PCM, text, aircraft IDs or positions. Existing app transport adds app/OS details
and pseudonymous installation/hardware IDs. Progress: at most once per five wall
seconds; existing global budget: 64 KiB/minute. HTTPS only for bearer tokens.
Public proxy exposes ingest/client-status/health only, not admin. Local reports
remain available if delivery fails. No automatic IQ upload.

## Reference Needed

Retain IQ format/rate/center, selected channel/type, JAERO version/settings and
decoded output/time interval. A recording cannot replay a voice channel outside
its captured bandwidth. Dad can compare this harness on that same file now;
actual protocol/voice acceptance must wait for the missing layers listed in
[Inmarsat scope](INMARSAT.md), not infer success from carrier counters.
