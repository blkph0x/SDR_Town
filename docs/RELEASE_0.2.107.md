# 0.2.107 - Manual bandwidth control

The main receiver now has a persistent Auto BW checkbox, enabled by default.
Uncheck it to hold your channel width across mode changes, band-plan tuning,
live classification and remote mode-only commands. Works in AUTO, NFM, WFM,
AM, USB, LSB and CW. The adjacent Detect BW button is disabled while unchecked.
Manual width edits, saved frequency presets and explicit remote widths remain
available. P25's protocol-required channel setup is deliberately unchanged.

The setting is included in runtime/control snapshots and changes are logged.
No NFM, WFM, HF or P25 DSP algorithm changes in this release. WFM partition
characterization is a separate diagnostic test, not a claim of WFM repair.
