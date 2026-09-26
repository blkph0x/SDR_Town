# 0.2.105 - NFM continuity and FM diagnostics

Portable experimental testing release. Fixes NFM callback-boundary decimation
phase and filter history, preserving existing coefficients and radio settings.
Adds bounded local FM stage/count diagnostics and opt-in remote summaries.
See [diagnostic guide and test commands](FM_DIAGNOSTICS.md).

P25 decode/follow/security, WFM and HF DSP algorithms and speaker buffering are
unchanged. Remaining resampler/WFM boundary issues are documented, not hidden.
This is not a claim of universal RF acceptance or completion of remote collector
deployment. No signed installer/updater channel replacement is included.
