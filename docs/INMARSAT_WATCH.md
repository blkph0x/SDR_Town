# Saved Aero watch channels

Experimental single-SDR automation. Rates such as 10500 and 8400 are **bits per
second**, not MHz ranges. Enter the actual RF frequency separately.

## Setup

1. Open **Tools > Inmarsat Aero**. Choose the SDR, a manual frequency and decoder,
   then **START / TAKE OVER** to see real signals in **Watch channels**.
2. Click a signal in the spectrum/waterfall. Choose **Aero data 10500**, data
   600/1200, burst data 1200/10500, or **Aero voice 8400** as appropriate. The
   frequency field remains editable. Add an optional name and **Add channel**.
   Saved channels have enabled checkboxes; select a row and Remove to delete it.
3. Add your known position-data and voice channels. EGC is deliberately not a
   watch decoder: its protocol/position/voice path is not implemented.
4. Stop, set timing and **Save timing**, enable **Automatic data / voice watch**,
   select speaker/record options and Start. Configuration is saved atomically in
   AppData's `inmarsat_engine.json`; reopening restores it without starting RF.
5. Stop restores the previous receiver and ordinary Listen audio. Explicit Tune
   or a band-plan channel switches back to manual operation. P25 is not retuned.

The CLI's `inmarsat start` uses the same saved watch configuration and worker.
IQ replay stays source-local and never attempts RF hopping outside its file.

## Cycle

- Enabled data and voice channels are grouped separately by actual sample rate,
  two independent decoders per group by default (adjustable 1-4), with RF/filter edge clearance.
  Each decoder gets the same chronological IQ within its group and owns separate
  carrier, framing and vocoder state. Frequency groups outside the passband need
  retuning; they cannot be watched simultaneously on one receiver.
- Each data group gets the minimum dwell. Continue up to the per-group maximum
  until the visit's distinct validated aircraft count reaches the target. Every
  group is visited at least once. Duplicate aircraft and earlier visits do not
  inflate the count. Reaching a target is not proof of complete coverage.
- Data dwell default: minimum 10 s, maximum 30 s/group, target 10 aircraft. No
  decoded positions yields **no positions decoded**, not a fake refreshed map.
  Partial refresh still proceeds to voice rather than waiting forever.
- Voice gets 12 s to acquire. After decoded speech starts, retain the group until
  6 s without new speech. Carrier lock alone does not extend it. Monitor one
  conversation at a time; other in-band decoders remain independent. Switch focus
  only after the selected conversation's idle hold. WAV records the selected
  speaker stream, not a mix of simultaneous conversations.
- Visit voice groups in order. After all are quiet, return to data. A refresh is
  also due after 180 s; ongoing speech defers it, with a hard 600 s maximum voice
  visit. That maximum can interrupt a call. All these timings are user policy,
  exposed in the panel, not protocol constants or promises of call completeness.
- Old PCM is discarded on group changes, source identity changes and IQ gaps.
  A hardware tune must be confirmed and the cursor re-anchored before new decoding.
  The RF lease and ordinary-Listen pause remain in force through the whole cycle.
- Map positions are last received reports. Gray means the receipt is older than
  the chosen refresh interval. Hover shows receipt age and transmitted report
  time. Green identifies the selected decoded voice AES when it has a position.

## Diagnostics

Local `inmarsat_diagnostics/*.jsonl` includes `watch_transition` and one-second
progress: phase, reason, current group/center/channels, visit count, single speaker
focus, each decoder's CRC/voice/codec results, IQ gaps and processing load. A load
ratio over 1 means processing exceeds incoming RF time: reduce concurrent decoders
or the SDR sample rate; inspect gaps before assuming an RF or codec failure.
Logs are capped at 8 MiB/session. Remote summaries are opt-in, rate-limited scalar
counts only; they exclude frequencies, names, identifiers, positions, text and PCM.

## InmarScope Comparison

Reviewed [InmarScope](https://github.com/SarahRoseLives/InmarScope/tree/26ae80af4bcfa4c86ed55f4383d1c95481d3450b),
not an assumption of identical software. No source was copied in this change.

| Stage | Comparison and intentional difference |
|---|---|
| Channel selection | Both attach rate-specific decoders to absolute RF frequencies. Town persists both voice and data channels; upstream's `saveDecoders` comment excludes 8400. |
| IQ/DDC | Both keep per-channel DSP and produce the 48 kHz modem input. Upstream uses complex 48 kHz and rate-specific DDC bandwidths (9 kHz voice, 21 kHz 10500 data); Town retains its tested 6.5 kHz baseband cutoff and 8 kHz real IF. These are not identical filter responses. |
| Carrier recovery | Both use JAERO-derived continuous OQPSK/MSK. Upstream currently caps OQPSK acquisition via `oqpsk_lockingbw=6000`; Town keeps its existing rate-based lock bandwidth. No speculative filter/PLL tuning without matched failing IQ. |
| Framing/FEC | JAERO-derived unique words, deinterleave, convolutional FEC, descramble and CRC. Town requires current C-frame valid signalling before releasing voice. |
| Vocoder | Exact matching 96-entry Aero row/column mapping, LSB-first 12-byte words, AMBE4800x3600, quality 1, persistent state and 160 mono PCM samples/8 kHz per word. Never P25 AMBE2450. Town also isolates synthesis randomness per stream. |
| Activity | Upstream increments activity even for subsequently rejected damaged words. Town uses CRC-framed, non-muted decoded words, not carrier lock or codec silence. Both distinguish acquisition grace from idle hold. This is not intelligibility proof. |
| Audio | One selected decoder to the speaker, separate per-channel codec histories. Upstream additionally drops words at `errs2>3` or RMS>18000. Town retains codec erasure/mute handling and current-frame gating, preserving measured replay output instead of importing unqualified thresholds. |
| Position | Town accepts validated binary ADS-C and displays last received positions. Neither tuning L-band data nor reaching a count guarantees all aircraft provide positions on that link. |
| Follow | Upstream follows fresh C-assigns and supports dual SDRs. This feature cycles an explicit saved list on one SDR; it does not create automatic assignment-follow or dual-device reception. |

Reference files: `src/voice/voice_ops.cpp`, `src/decode/decoder.cpp`,
`src/voice/ambe_decoder.cpp`, `src/state/config.cpp`.

Live qualification still needs the tester's antenna setup and matched data/voice
IQ, settings and expected speech/positions. Unit tests and codec reference parity
prove software behavior, not that an arbitrary satellite channel contains voice.
