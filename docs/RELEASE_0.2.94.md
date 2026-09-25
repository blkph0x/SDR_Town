# 0.2.94 Experimental Inmarsat Receiver Handover

Fixes the missing transition from a P25-configured SDR to Inmarsat. Previously
START / TAKE OVER could pause ordinary Listen but only refused P25, including
receiver flags left after hardware stopped.

## What Changed

- Start asks before stopping P25 on the selected receiver. No is the default.
- Confirmed handover stops CC monitoring, talkgroup follows and warm standby;
  queued P25 voice is discarded before satellite reception starts.
- Configuration is rechecked after confirmation. Busy decoders return a retry
  message, and invalid empty watch lists cannot stop P25.
- Stop or failed Inmarsat startup does not silently resume P25. Use Monitor CC
  when you want P25 again. Unrelated-device reception is not deliberately stopped.
- Local control automation can probe with `action:"prepare"` and must supply
  both `force:true` and `stopP25:true` on Start to explicitly leave P25.
- The P25 decoding, FEC, vocoder and audio algorithms are unchanged. The existing
  host ownership guard remains, including for unconfirmed CLI/API requests.

## Tester Checks

1. Monitor a P25 CC, open Tools > Inmarsat Aero and press Start. Choose No and
   verify P25 stays selected. Press Start again and choose Yes to switch.
2. Confirm the P25 status says stopped, auto-follow is off, and the Inmarsat
   panel reaches live reception. Repeat after stopping hardware while on P25.
3. Stop Inmarsat: P25 must stay off until Monitor CC is selected again.
4. With a second SDR, select the unused receiver for Inmarsat and verify the
   first device's P25 configuration is not stopped.
5. For a failure send the exact message and short Inmarsat/P25 diagnostic log.

Saved Aero watch channels and timing remain supported. This is a handover fix,
not a new claim of field-qualified satellite speech. Matching known-clear IQ
is still needed to qualify the reported tone/no-voice issue.

Diagnostics remain opt-in at https://gearsqueens.online/sdr-town-diag/ingest.

## Verification

- Full local suite: 11/11 test groups pass; confirmation, cancellation and
  startup-failure cases included.
- Actual RTL/GUI handover passes for live P25, configured-only P25 and armed
  auto-follow; the extracted portable package passes too.
- GUI/CLI fast/paced Aero reference PCM is byte-identical to v0.2.93.
- [Independent Windows CI](https://github.com/blkph0x/SDR_Town/actions/runs/36079172152)
  passes build, tests and packaging. All eight public assets and updater
  signature were downloaded and verified.
