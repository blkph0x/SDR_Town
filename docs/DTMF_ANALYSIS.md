# DTMF analysis

Receive-only, experimental. DTMF detections never transmit, execute commands,
retune, gate speaker audio or change P25 behavior.

## Live reception

1. Select NFM and receive the channel normally. In **Workspace > Repeater
   Tones**, enable the repeater monitor. Enable **Log DTMF** for its heard list.
2. Open **Tools > DTMF Analysis**. Select tuned/output or input; input requires
   the existing dual-watch setup and both channels inside the IQ passband.
3. Choose **Conservative (40 ms)** or **Fast bursts (20 ms, experimental)**.
   Press **Apply to Repeater Tones**. Both DTMF observers get the same profile
   at their next block, with an explicit state reset. Other decoders do not.
4. The table shows the latest 64 digits, source epoch, start/confirmation sample
   indices, RF frequency and tone evidence. Save exports bounded diagnostic
   JSON including rate, transformation, rejection/overflow counters and cost.

Settings are restored when this window opens, but require Apply to affect the
current receiver. Applying settings does not enable reception or dual-watch.
An inactive/stopped source is labelled **No recent NFM samples**. Existing
Repeater Tones still lists completed sequences after 300 ms without tones.
Sequences display up to 32 digits; truncation and overwritten events are counted.

The input leg now consumes every chronological IQ block. Previous releases
processed only one in four, losing bursts and resetting tone history.
Real IQ loss still resets the detector; missing samples are not reconstructed.
Dual-watch uses more CPU because it now actually processes both channels.

## Inversion and shifted tones

- Reversed audio polarity or phase needs no special setting: tone power is
  unchanged. Both are covered by the detector's magnitude calculation.
- Frequency inversion is different. Select **Frequency inverted** and enter
  the known inversion frequency. The default 3300 Hz is only a starting value,
  not automatic identification of a scrambler.
- A known pitch/time-scale change can be entered as a multiplier (0.8 to 1.2),
  with an optional -500 to +500 Hz shift. The normal expected frequency is
  `nominal * multiplier + shift`; inverted is `inversion - expected`.
- Tones must remain inside the input Nyquist band. The window rejects invalid
  configurations; it cannot recover tones removed by a receiver filter.

Unknown scrambling, arbitrary symbol remapping, severe masking and encryption
are not automatically recovered. A wrong transform may detect nothing. Fast
mode relaxes timing, not authenticity: review suspected digits against audio.

## Recordings and automation

Select **Audio recording**, open mono WAV/FLAC/MP3 (8-96 kHz, at most 120 seconds),
then Analyze. This uses the exact same detector as the live NFM tap, with source
sample timestamps rather than wall-clock playback speed. Stop/close cancels the
owned worker. EOF explicitly completes the sequence without artificial silence.
No recording or decoded sequence is uploaded automatically by this feature.

```powershell
SDR_Town.exe --cli --no-control-server --no-remote-diagnostics --cmd 'tones dtmf "capture.wav"'
SDR_Town.exe --cli --no-control-server --no-remote-diagnostics --cmd 'tones dtmf "burst.wav" --fast'
SDR_Town.exe --cli --no-control-server --no-remote-diagnostics --cmd 'tones dtmf "inverted.wav" --fast --invert-hz 3300'
SDR_Town.exe --cli --no-control-server --no-remote-diagnostics --cmd 'tones dtmf "shifted.wav" --scale 1.12 --shift-hz -120'
```

The JSON report records the last 64 detections with each detection's actual
profile. `twistDb` is signed, column minus row; it was absolute in older builds.
Start/confirmation samples are analysis evidence, not exact transmitter key edges.
`purity` is normalized pair energy, not a probability that the decoded text is
genuine. Processing microseconds are local observer cost, not latency on air.
Row/column frequency values name the selected analysis hypotheses; they are
not precision frequency measurements. A known transform is tested, not inferred.

## Qualification and limits

Independent generated fixtures cover all 16 keys, 20 ms fast bursts, repeats,
8/11.025/44.1/48/96 kHz, sample partitions, polarity, spectral inversion, known
shift/scale, ordinary +/-1.5% frequency errors, DC offset and negative signals.
Tests also exercise dual-channel 2.4 Msps NFM demodulation and GUI/file parity.
Synthetic evidence is not a telephone certification or a field capture result.
Speech talk-off corpora, every codec and unknown obfuscation are not qualified.

Algorithm/timing references and decisions: DEC-0168 in [DECISIONS](DECISIONS.md),
[ITU Q.24](https://www.itu.int/rec/T-REC-Q.24-198811-I),
[SpanDSP detector](https://github.com/freeswitch/spandsp/blob/master/src/dtmf.c),
[DTMF receiver research](https://users.ece.utexas.edu/~bevans/papers/1998/dtmf/dtmf.pdf).
No SpanDSP source or new third-party library was copied into this implementation.
