# Saved Aero watch channels

Experimental single-SDR automation. Rates such as 10500 and 8400 are **bits per
second**, not MHz ranges. Enter the actual RF frequency separately.

0.2.98 corrects the former interpolated preset frequencies. The table now uses
individual published channel centers with their actual survey rate, displays
four decimal MHz places, and labels the rate bit/s. Plans name their survey date;
hover over the table for provenance. These are historical reference presets,
not guaranteed active assignments. Existing saved watch channels are not
silently retuned: remove/re-add incorrect entries from the corrected table or
enter your locally verified frequency. 6F1 has data-only presets because the
source explicitly marks its older voice list obsolete. EGC presets without a
verified source have been removed; manual tuning remains available.

## Setup

From 0.2.99, explicitly choosing a decoder rate previews a matching preset
frequency from the selected satellite survey. Tune/Start commits that selection.
An already matching frequency is kept. No surveyed match (including burst/EGC)
leaves the frequency unchanged with a visible status; manually select the known
signal. Choose the rate before clicking a custom waterfall frequency. Restoring
saved controls does not select presets or retune. There is no fixed worldwide
frequency range determined solely by 600/1200/8400/10500 bit/s.

1. Open **Tools > Inmarsat Aero**. Choose the SDR, a manual frequency and decoder,
   then **START / TAKE OVER** to see real signals in **Watch channels**.
2. Click a signal in the spectrum/waterfall. Choose **Aero data 10500**, data
   600/1200, burst data 1200/10500, or **Aero voice 8400** as appropriate. The
   frequency field remains editable. Add an optional name and **Add channel**.
   Or choose the rate first, enable **Click to add**, and click several signals.
   Repeating the same frequency/rate selects its saved row instead of duplicating.
   Saved channels have enabled checkboxes; select a row and Remove to delete it.
3. Add your known position-data and voice channels. EGC is deliberately not a
   watch decoder: its protocol/position/voice path is not implemented.
4. Set timing and **Save timing**, enable **Automatic data / voice watch**.
   Channels/timing/watch enable can be changed while receiving: they apply at the
   next IQ block boundary, flush old PCM and reacquire the new group. Stop first
   to change speaker/record options. Configuration is saved atomically in
   AppData's `inmarsat_engine.json`; reopening restores it without starting RF.
5. If P25 is configured on the selected SDR, Start asks whether to stop P25 and
   switch. No/Cancel leaves P25 unchanged. Yes stops its CC/follow session and
   discards queued P25 audio before Inmarsat starts. P25 is not resumed on Stop
   or a failed hardware start: select Monitor CC explicitly to restart it.
   P25 on a different, unrelated receiver is not stopped by this prompt.
6. Stop restores the previous receiver and ordinary Listen audio, but not a P25
   session you explicitly stopped. Explicit Tune or a band-plan channel switches
   back to manual operation. CLI start retains the existing P25 refusal.

Local control automation: POST `/v1/inmarsat/control` with `action:"prepare"`
probes readiness without stopping P25. `requiresP25Stop:true` requires explicit
operator consent. Only `action:"start", force:true, stopP25:true` requests that
handover; `force` alone is insufficient. The GUI button and this API call use the
same host preflight. Existing loopback authentication still applies.
`action:"watch", watch:{...}` validates/saves the full watch object and applies
live edits through the same block-boundary path. Empty enabled lists are rejected
while receiving. Success acknowledges saved settings; inspect diagnostics for
the new active group rather than assuming asynchronous reacquisition is complete.

The CLI's `inmarsat start` uses the same saved watch configuration and worker.
IQ replay stays source-local and never attempts RF hopping outside its file.

## Cycle

- Enabled data and voice channels are grouped separately by actual sample rate,
  two independent decoders per group by default (adjustable 1-4), with RF/filter edge clearance.
  Each decoder gets the same chronological IQ within its group and owns separate
  carrier, framing and vocoder state. Frequency groups outside the passband need
  retuning; they cannot be watched simultaneously on one receiver.
  Each active decoder now owns a persistent thread. There is one shared immutable
  IQ block in flight, no growing per-channel backlog. All results are joined in
  channel order before messages and single-speaker selection. Default remains two;
  increase Concurrent decoders to four when processing load stays below 1.
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

The four-quadrant display is a **constellation diagram**. Its selector changes
only which channel is inspected, not speaker focus. Each worker supplies its own
bounded, recovered modem symbols. **Protocol lock** uses validated frame/DCD
state, not the appearance of four clusters. Inactive groups/no fresh symbols
clear the plot. MSK and burst modes need not look identical to continuous OQPSK.
Points stay local and never enter remote diagnostics.

Successful manual Tune/Start or band-plan preset tuning selects **Current / first
active channel**, releasing any saved watch-channel selection. Rate/frequency
previews alone do not change the inspected decoder. The diagram labels its
actual decoder frequency/rate. Selecting a saved channel outside the active
group shows **Not in active group**, never the first channel's dots. **No fresh
symbols** means the active modem has no recent scatter update; **No active
decoder** means no current decoder snapshot is available. Quiet burst channels
may have no points between transmissions. EGC has no native constellation path.

Visual polling is 20 Hz; status/map/messages remain 2 Hz. Only fresh FFT values
advance waterfall history. Actual spectrum frame rate is limited by the radio's
existing FFT producer (currently about 12 Hz for hardware), not fabricated extra
RF frames. P25/global spectrum processing is unchanged.

Local `inmarsat_diagnostics/*.jsonl` includes `watch_transition`, `watch_reconfigured` and one-second
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
