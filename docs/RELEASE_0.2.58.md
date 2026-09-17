# SDR Town 0.2.58 (experimental)

## Recorded SSTV image workspace

Open **Tools > SSTV Recorded Images**, choose a mono WAV/FLAC recording and a
new output directory, then Decode. Automatic, Robot36 and Martin1 modes are
available. Images appear scanline by scanline while decoding; completed/partial
pictures remain in the list with full-resolution PNGs and a JSON report.
Use Open output to access saved files. Source audio and existing output folders
are never overwritten.

Background single-job decoding keeps the UI responsive. Cancel/close requests
stop the worker and its helper; final output publication finishes if already
started. Preview transfer keeps one latest image instead of accumulating GUI
events. Transport rejects duplicate/malformed rows and checks preview pixels
against final RGB data before PNG publication.

The CLI commands from 0.2.57 remain available and use the same file decoder.
Input limits: mono 8..96 kHz, <=360 seconds /128 MiB for image decoding, four
images/job, 120-second helper deadline. Completed rows do not guarantee a
noise-free picture. All SSTV features here are experimental recorded reception.

## Scope

Local gates: 275 core and 17 Qt cases pass. An additional fixture-dependent
Qt case passes separately on four independent full/partial recording runs;
GUI output matches direct decode. Transport/cancellation/teardown and existing
RDS/CTCSS/DCS/registry/SSTV CLI regressions pass. Four main GUI workspace sizes
pass. This is recorded-input and local-host evidence, not live RF acceptance.
Extracted portable CLI/workspace checks pass. SSTV window harness also passes
four full/partial cases against packaged helper/DLLs. Installer/ZIP hashes and
Ed25519 update signature verify; test executables are not in the distributed ZIP.

Live RF SSTV, more image modes, AX.25 and public/weather satellite decoders
remain next work. No P25 DSP, slot/security gate or audio timing changes.
Existing 14 P25 source-string verification failures remain tracked as T-0029;
this release does not claim universal Phase 2 clear-audio acceptance.

Installer, portable ZIP, standalone control DLL, SHA-256 sums and signed update
manifest are supplied. Experimental channel is GitHub Latest for tester updates;
executables are not Authenticode-signed. No private key or test capture ships.
