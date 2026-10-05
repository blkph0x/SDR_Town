# SDR Town 0.2.123 experimental

Portable Windows tester release. This does not replace the signed-installer
update channel. Keep a copy of your working installation and settings.

## Included changes since 0.2.122

- Workflow device ownership and named Inmarsat, Satcom, Aircraft and SSTV
  controller work now available for remote testing. Repeated independent P25
  controllers and physical multi-radio acceptance remain unfinished.
- P25 control results are fenced against stale tuning context; control reads
  do not bridge major retunes. Initial applied-frequency metadata is validated.
- Passive P25 diagnostics no longer run alternate vocoder synthesis that can
  affect normal output. Bounded timing/decision events use existing capture logs.
- Session-bound selected traffic teardown and traffic observer improvements.
- Exact dependency sources, notices, compiler/runtime provenance and successful
  Qt/USB replacement tests are required by the publishable-package verifier.

## Distribution

Original SDR Town contributions retain MIT. The RTL-enabled combined binary
uses GPL-3.0-or-later, with original component notices and applicable runtime
exceptions preserved. Sources and rebuild/replacement instructions accompany
the portable package. See DISTRIBUTION.md; no claim of patent clearance.
SDRplay's official API/service must still be installed separately.

## Known limits

P25 missed-response work is not complete: control-channel scheduling can leave
IQ unexamined, and accepted short audio tails can remain buffered at teardown.
Do not interpret a successful build or individual clear call as all-call
acceptance. This release does not loosen encryption/slot checks or add a
buffer/timeout workaround. Testers' logs and recordings remain subject to the
existing telemetry and separate recording consent controls.
