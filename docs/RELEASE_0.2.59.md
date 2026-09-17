# SDR Town 0.2.59 (experimental)

## Live NFM SSTV

Tools > SSTV Images now offers Recording or Live NFM - main receiver. Start
the main receiver in NFM, choose a new output folder and Receive. Robot36,
Martin1 and Automatic remain the qualified image modes. Finish and save drains
queued samples and saves validated complete/partial PNGs plus a JSON report;
Cancel discards provisional output. Existing recorded CLI/GUI remains available.

Live input uses the raw discriminator before speaker processing. Bounded queues,
continuous rate conversion and a worker-owned helper keep decoding off RX/GUI
threads. Retunes, gaps, overruns and incompatible modes abort rather than splice
streams. Sessions are limited to six minutes/four images; explicit restart is
required after interruption. P25 decoding and speaker processing are unchanged.

## Validation and limits

Independent full/partial recordings test the combined live GUI path against the
converted file decoder pixel-for-pixel. Native tests cover input ordering,
fractional rates, lifecycle, cancellation and invalid streams. This is not proof
of an off-air SSTV image on this hardware: a known transmission is still needed.
HF USB/LSB live input, additional modes and satellite payload decoders are not
included. Martin1 retains the previously documented reference-image differences.

Existing P25 acceptance/static-verifier gaps remain documented; this release
does not claim to fix or requalify P25.
