# 0.2.110 follow-up audit (2026-09-27)

Reviewed clean HEAD 787488e. Runtime source is 1f7740e; the intervening commit
only records publication evidence. This is a bounded review of the supplied
rate-plan/AUTO/tone/HF proposals, not certification of every decoder.

## Corrections that change implementation priority

### AUTO is wired through a different classifier

`classifyModeAround` has no receive callers, but that is not the active selector.
`chooseSmartModeAndBandwidth` in src/P25VoiceDecode.cpp uses
AdvancedSignalClassifier, band-plan priors, confidence and bandwidth fallback.
Callers include src/CliApp.cpp:1159, src/MainWindow.cpp:5480 and the analog
worker fallback in src/MainWindowP25Orchestration.cpp:535. The latter file name
does not make this branch P25: it explicitly requires !monP25VoiceDecode.
The core AUTO-to-NFM fallback and later AUTO arms do leave dead branches.
Do not wire a second competing classifier just to make the old helper used.
Follow-up should test mode persistence, empty-spectrum fallback and manual-BW
policy across GUI and CLI; dead-branch cleanup is separate from a routing bug.

### WFM is filtered before decimation, not an unfiltered skip

src/Demod.cpp designs a full-rate channel FIR and runs wfmSpeechFir before
retaining samples. A FIR followed by downsampling is a valid structure; a
decimating FIR can avoid unused output computations, but its placement alone
does not prove aliasing. Reference: https://www.liquidsdr.org/doc/firdecim/.
Whether coefficients adequately reject each alias band must be measured.

The target rate is max(192000, min(max(180000, BW*1.1), sr)), capped at sr.
For 220 kHz BW and 2.4 MS/s input, rounding gives M=10, or 240 kS/s, not
192 kS/s. For 180 kHz BW it gives M=12, or 200 kS/s. Complex IQ spans
[-fs/2,+fs/2]; a 200 kHz *total* channel does not itself demand fs>400 kS/s.
Transition margin and FM distortion are separate constraints. A mandatory
final 192 kS/s stage cannot preserve a full 220 kHz complex channel.

Fresh 36-case benchmark: build/fm-audit-110.json. At +40 dB first-image
blockers, WFM PCM difference vs clean is -68.28/-74.03/-56.56 dB at
2.048/2.4/10 MS/s. Wanted gain error is at most 0.00112 dB in those cases.
These are 180 kHz channel, finite offsets, synthetic IQ without ADC overload;
they neither prove all WFM settings nor justify claiming a current universal
alias defect. 10 MS/s processing/input ratio was about 0.659 on this host.

Next WFM gate: sweep requested bandwidth, actual rounded output rate, offsets,
high-deviation wanted signals and blocker modulation. Include the independent
RDS/MPX branch, total CPU, squelch level, reset, tiny chunks and PCM counts.
Only then choose a multistage or retained-output FIR design. Preserve the
full-rate power estimator's semantics explicitly if computation is removed.

### Short CTCSS energy windows are not a reverse-burst detector

src/CtcssDecoder.cpp does use one-second Hann windows, the 38-tone table and
best>4*runner. Its published lock needs two successful windows. Merely running
the same rules at 50-80 ms is not equivalent: independent direct projection
of a clean 67 Hz sinusoid at 8 kS/s over 80 ms gives energy(67)/energy(71.9)
=1.22045, below the required 4. The short window cannot separate these close
tones under the current rule. Steady-window magnitude energy also discards
phase, so a phase reversal requires phase-continuous evidence, not just purity.
Retain slow identification; investigate a fast tracker conditioned on an
already identified tone, with phase/noise/fade/reset fixtures. Keep experimental
until real-radio as well as synthetic acceptance; do not promote DCS implicitly.

## Confirmed bounded observations

- P25 streaming DDC is default-off; receiver realtime enable is experiment-gated
  and independent-source-only. DEC-0014 documents a measured regression. Keep
  it out of this analogue pass, including a live-only default flip. Contiguous
  sample ownership and a passing live/replay comparison must precede adoption.
- Phase 2 config selects 6000 symbols/s; soft AMBE mask phase lock remains off.
- HfDemod SSB/AM use 257 taps, CW 401, and CW pitch is 700 Hz. A larger tap count
  is not a quality specification: measure passband, rejection, latency and CPU
  before selecting 401. Do not change the pitch default as part of filtering.
- ReceiveDecoder registers CTCSS/DCS experimental and RDS nonexperimental;
  its RDS input contract is 128-384 kS/s. This range alone is not RDS validation.
- InmarsatDemod probe still forces locked=false; it is not the voice FEC engine.
- ModeS.cpp limits returned frames to 64; this is not evidence of a 16k GUI cap.
- AptImageDecoder calls normalizeLine; telemetry-wedge calibration requires
  a separately specified acquisition/calibration test, not just contrast tuning.

## Revised next-pass order

1. Broaden WFM characterization, then optimize/repair only reproduced failures.
2. Test existing AUTO policy end-to-end; remove dead helper/arms separately.
3. AM/HF sensitivity, sideband rejection and continuity characterization.
4. CTCSS locked-tone fast phase tracker, independent from idle identification.
5. Existing capture-event schema: bounded decision events with sample index,
   receiver/session identity and reason, explicit dropped-event counters, no
   synchronous disk/network writes in DSP. Remote forwarding remains opt-in.
6. APT calibration and WFM stereo/SCA as separate feature tasks.

No production behavior was changed by this audit. P25 remains deferred. The
remaining survey/geolocation/protocol feature inventory is not certified by
this bounded review. Do not claim calibration or feature completion from it.

## Validation

- Existing FM benchmark: 36/36 measurements, parser validation PASS.
- `[wfm],[ctcss],[hf],*classifier*`: 9104 assertions / 41 cases PASS.
- No new binary needed: verified experimental v0.2.110 remains current.
