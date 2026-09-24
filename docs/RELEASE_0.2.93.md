# 0.2.93 Experimental Aero Watch And Receiver Repairs

## New

- Click live spectrum/waterfall signals and save Classic Aero data/voice channels.
- Automatic position collection, voice-group monitoring, idle return and bounded
  periodic position refresh. Configuration survives application restarts.
- Independent modem/codec state, one speaker focus, confirmed RF transitions and
  PCM flushing on source changes. Two concurrent decoders by default (1-4
  selectable), processing-load/gap diagnostics and stale map positions.
- Inmarsat START honors visible manual settings; panel opening preserves voice
  mode. Takeover parks ordinary Listen audio and restores it on Stop.
- SDRplay discovery verifies registration, retries after Rescan, adds runtime
  paths and reports vendor service state. Official API and matching Soapy plugin
  must be installed separately; no vendor drivers are redistributed.
- SSTV receiver detach no longer competes indefinitely with a hot publisher.

P25 DSP, vocoder, follow and audio paths were not edited. The separately reviewed
DeviceManager delta is SDRplay runtime loading only. FUBAR is unchanged.

## Test This

1. In **Tools > Inmarsat Aero**, use manual reception to populate **Watch channels**.
   Click signal, choose decoder rate, Add channel. Stop, set/save timing, enable
   **Automatic data / voice watch**, then Start.
2. Confirm data/voice visits and idle return; check position age and processing
   load. An idle carrier must not strand the cycle. Stop restores ordinary Listen.
3. Close/reopen and verify channels and policy persist without automatically
   starting hardware. For voice use a confirmed 8400 C-channel, not 10500 data.
4. Send a short matching IQ and local `inmarsat_diagnostics` JSONL for unexpected
   transitions, overload, tones or no voice. RSP testers: report model and new
   runtime/service diagnostics, then confirm live IQ after Devices/Rescan.

See [watch guide and InmarScope comparison](INMARSAT_WATCH.md),
[IQ replay/automation](INMARSAT_IQ_TESTING.md) and [SDRplay help](SDRPLAY.md).

## Limits

Experimental tester build, not fully qualified satellite voice. Public JAERO
recordings verify framed PCM and ADS-C coordinates; the voice reference is
mostly codec silence. Live clear speech still needs a matched tester reference.
Saved-list cycling is not assignment-driven follow. Dual-SDR reception, aircraft
photos, encrypted services and full EGC protocol decoding are not included.
One SDR cannot watch out-of-band groups simultaneously. Maximum refresh deadlines
can interrupt calls. WAV recording contains the selected conversation, not all
concurrent decoders.

Remote diagnostics remain explicit opt-in, using
https://gearsqueens.online/sdr-town-diag/ingest. Summaries are bounded scalar
telemetry, never automatic IQ/PCM, aircraft identity or location uploads.
