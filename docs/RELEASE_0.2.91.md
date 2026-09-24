# 0.2.91 Experimental: Inmarsat IQ Diagnostics

## What To Test

Open Tools > Inmarsat Aero > Open IQ replay. Load SigMF, stereo IQ WAV or explicitly
configured raw IQ. Check sample count, rate, center, pause/seek and final report.
Use the same recording in JAERO and retain its settings/output for comparison.
See [formats, CLI automation and report fields](https://github.com/blkph0x/SDR_Town/blob/v0.2.91/docs/INMARSAT_IQ_TESTING.md).

The optional sharing checkbox sends compact diagnostic counters through the HTTPS
collector. It does not upload IQ or audio. Packages default to reporting off;
existing opted-in local settings remain active. Force off: --no-remote-diagnostics.

## Important Limit

This release is **not an Inmarsat voice decoder**. Unique-word/FEC/CRC, validated
voice assignments and the Aero-specific mini-m codec remain missing. The previous
unused wrapper's false Aero support claim is fixed. No voice/audio is synthesized
from unvalidated bits. No green talking-aircraft markers or aircraft photos have
been added yet; those require validated call identity and actual photo retrieval.

P25 DSP/audio and analog demodulators are unchanged. No FUBAR source changes.
The diagnostics proxy addition leaves other site routes untouched.

## Verification

Native reader/pipeline/replay failure-case tests; real Qt GUI pause/resume/EOF;
GUI/CLI paced/fast black-box parity and malformed-input exit checks passed,
including the extracted portable package. Full local CTest passed 4/4;
[independent Windows CI](https://github.com/blkph0x/SDR_Town/actions/runs/35993275475)
passed. All eight uploaded asset hashes and sizes match the tested release.
Four actual replay session summaries were verified in the HTTPS collector's logs.
Tests prove replay/diagnostics transport, not RF or intelligible Aero voice.
Live reception qualification awaits a reference recording and an off-LAN tester
must still verify diagnostics reachability from their network.
