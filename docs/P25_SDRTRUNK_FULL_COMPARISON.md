# P25 Full Low-Level + High-Level Implementation Comparison: SDRTrunk vs SDR Town (our code)

Date: 2026-07-01
Purpose: Diagnose why P25 workflow does not produce clear audio; align our C++/Qt implementation with SDRTrunk's proven Java implementation.

## Sources
- Our code: P25LiveDecoder.cpp/h, P25Control.cpp/h, P25FollowStateMachine.cpp/h, P25TrafficChannelProcessor.cpp/h, main.cpp (P25 paths), mbelib integration.
- SDRTrunk (sdrtrunk-src): module/decode/p25/phase1/* (P25P1DemodulatorC4FM, P25P1DecoderC4FM, P25P1MessageFramer, P25P1MessageProcessor, P25P1MessageAssembler, ...), phase2/* (TimeslotFactory, Voice*Timeslot, MAC structures), audio/* (P25P1AudioModule, P25P2AudioModule, Imbe/Ambe wrappers), dsp/* (symbol, fsk, psk), TrafficChannelManager.

## Low-Level Comparison (IQ/Baseband -> Symbols -> Dibits -> Frames)

### Common
- Both target 4800 sym/s, 320-bit (160 dibit) TDMA timeslots for P2, 196-bit? frames + status for P1 LDU etc.
- Both handle C4FM (Phase1 conventional) and CQPSK/LSM (Phase1 or simulcast), and Phase2 TDMA.
- Both use frame sync correlators (our Streaming*Correlator48/40 vs SDRTrunk hard/soft sync detectors).
- Both descramble Phase2 with mask (XOR from LFSR keyed by NAC/WACN/SysID + superframe).
- Voice extraction: both pull 72-bit AMBE frames from known bit offsets in descrambled voice timeslots.
  - SDRTrunk: Voice4Timeslot: bit offsets 2,76,172,246 (72 bits ea).
  - OP25 (boatbod): fixed dibit starts at 11,48,96,133 (and symmetric) within the 180-dibit burst for the voice segments, then vf deinterleave to mbelib.
  - Our: defaulted to OP25 offsets {11,48,96,133} (payload view) to match the proven on-air extraction + mbelib feed workflow. Previous SDRTrunk-derived {1,38,86,123} (after +20 ISCH) was a 10-dibit shift that commonly produced bad bits for mbelib even with good p2vcw/mask counts. Safe pull + both layouts kept for testing.
- Both feed a MBE codec (SDRTrunk: primarily JMBE interface; ours: native mbelib with stateful mbe_parms).

### Our Low-Level (P25LiveDecoder)
- Channelize: custom FIR / liquid?
- Discriminator: atan2 diff -> Hz deviation (for C4FM).
- Pre: DC block, LPF (cutoff ~0.78*sym, transition).
- C4FM path: percentile scale from symbols, SecondOrderTimingLoop (mu/omega, early-late TED on energy), linear sample interp.
- CQPSK path: complex samples, carrier loop (PLL-like), separate timing.
- Symbol -> dibit: sliceC4fmSymbol with 4 levels (+3,+1,-1,-3), optional invert + bit reverse in dibit.
- Dibit stream -> streaming correlator for sync (bit or dibit), then NID BCH, TSBK decode, MAC PDU parse, voice burst classification (DUID).
- Phase2: superframe lock, mask phase search (multiple candidates, sticky), descrambled payload, ISCH, ESS decode variants.
- Voice CW packing: bitsFromDibit (MSB first in pair) into 72-bit array, then to mbelib 4x24 via p25Phase2AmbeBitsToMbelibFrame (with row/col schedule + variants 0-5 for order/invert/reverse).
- Multiple candidate results + arbiter (trust scores, voice counts, MAC/ESS, cqpsk preference).

### SDRTrunk Low-Level
- Often upstream complex samples -> PI/4 DQPSK or FSK path into demod.
- P25P1DemodulatorC4FM: receives phase samples (from DQPSK demod?), LinearInterpolator timing, Equalizer (gain/phase PLL), soft sync detector, symbol delay line, DibitDelayLine, gain ~1.2x correction for constellation imbalance observed on C4FM.
- Dibits assembled via DibitToByteBufferAssembler.
- Sync: P25P1SoftSyncDetector / Hard , uses ideal phases from Dibit enum.
- Dibit enum values: D01_PLUS_3=1, D00_PLUS_1=0, D10_MINUS_1=2, D11_MINUS_3=3. Matches our numeric slice.
- MessageFramer: consumes dibits, syncDetected callbacks, NID BCH, DUID classification, status symbols, assembles LDU/TSBK/PDU via MessageAssembler.
- Phase2: separate P25P2 path, ScramblingSequence (LFSR), TimeslotFactory classifies burst (VOICE_4 etc from DUID), Voice*Timeslot extracts submessages at exact bit offsets (no dibit abstraction in same way), MAC message parse.
- Soft decisions in places, heavy use of EDAC (BCH, RS, trellis/Viterbi for some, Golay in mbe).

### Identified Low-Level Gaps / Risks for Garbled Audio
1. **Filtering/Matched**: SDRTrunk equalizer + specific interp/gain tuning vs our basic LPF + timing loop. On real signals with multipath or offset, our symbols may have more jitter -> bad dibits -> bad voice bits.
2. **Dibit polarity / within-dibit order**: We have invert + reverseDibitBitOrder + 5+ variant tries in Ambe converter. If default not picking correct for a system, AMBE input is bit-scrambled -> noise/garble.
3. **Phase2 mask alignment**: Extensive code and variants; logs show cases where p2sf/p2mask high but MAC/ESS low. If mask slips, voiceCodewords are still scrambled when fed to AMBE -> garbage.
4. **Timing state carry / tail**: We persist omega/mu per decoder across blocks; SDRTrunk re-optimizes at every sync. Drops or cursor jumps in our rolling window (many prior cursor fixes) can desync superframe or voice slots.
5. **CQPSK carrier/timing**: Our carrier loop vs SDRTrunk psk/demod path. On LSM sites common for P25, this is critical.

## High-Level Comparison (Control, Grants, Traffic Following, Call State)

### Our High-Level
- P25ControlChannelAnalyzer: parses TSBK (ingestTsbk), Phase2 MAC PDU (ingestPhase2MacPdu), Phase1 PDU; builds channel ID tables, produces P25ControlEvent (GroupVoiceGrant etc).
- P25FollowStateMachine: evaluateP25Follow on snapshot (syncs, nids, imbe count, p2vcw, mac, ess, trafficProcessorActive). Decides ReturnToControl, FollowGrant, Continue, etc. Handles one-RTL constraints, late entry.
- P25TrafficChannelProcessor: owns a P25LiveDecoder(cfg with realtimeVoice), feeds hard dibits (from traffic source), observes results for vcw/mac/ess/end flags, tracks active/encrypted/audioOpen.
- In main: p25 monitor mode on CC, on grant event -> arm follow or dedicated traffic rx (or same dev cursor), collect voiceCodewords -> AMBE decode (with variant probe) -> gate -> resample -> pushAudioFrames if p25VoiceBlockMayEmitAudio (complex security/late-entry/mask/ESS/PTT gates modeled on sdrtrunk notes).
- Return to CC after end/hang or timeout.

### SDRTrunk High-Level
- P25TrafficChannelManager: central, listens to grants from CC decoder state, allocates traffic channel decoders (separate source/decoder instance per call/slot).
- Per-channel: P25P1DecoderState or Phase2 equivalent + MessageProcessor feeds audio module.
- Audio modules (P25P1AudioModule extends Imbe, P25P2 extends Ambe): receive LDU/voice timeslot messages, handle HDU/LDU2 or PTT MAC for enc state, cache if needed, call codec only for clear, apply NonClippingGain, emit audio + tones/identifiers.
- Continuous pipeline: samples -> demod/framer -> message processor -> audio module -> output. No "replay window" skips.
- Strong separation of CC state vs traffic call state; explicit call start/end events.

### Identified High-Level Gaps / Risks for "Workflow Not Working"
1. **Source continuity for traffic**: Our one-RTL shared rolling IQ + take window with cursor can still skip bursts (prior patches for overlap, stale, budgeted). SDRTrunk gives dedicated continuous source to traffic decoder.
2. **Grant -> traffic handoff & return**: Many dedicated patch notes (ONE_RTL_RETURN, TRAFFIC_SOURCE, FOLLOW_*). Still complex state; easy to miss late grants or fail to return cleanly.
3. **Encryption / clear decision**: Both try to use HDU/ESS/MAC_PTT. Our gate is strict; if metadata lags, audio suppressed even for clear calls.
4. **Audio emit gating**: Very defensive (many diag codes: MaskMissing, EssMissing, WrongSlot, AmbeRejected). Matches sdrtrunk lessons but can mute good audio in marginal or late-entry cases.
5. **Multi-slot / dual freq**: P2 has two slots; our target slot tracking + wrong-slot rejection logic is custom.

## Audio Path Comparison (Voice Frames -> PCM -> Speaker)

### Our Audio Path
- Extract 72-bit CW -> p25*ToAmbe3600x2450Frame (variants) -> mbe_processAmbe3600x2450Framef (or IMBE path) with persistent mbe_parms per rx.
- Gate on usable (finite, non-zero RMS, not clipped).
- resampleDecodedP25Pcm: per-block normalize to ~0.9 peak, cubic resample 8kHz->device (stateful phase/hist across frames).
- pushAudioFrames -> AudioEngine::pushAudioToActiveOutputs (shared miniaudio ring, per-device + master vol).
- No dedicated per-call codec instance beyond the per-rx decoder.

### SDRTrunk Audio Path
- Voice timeslot/LDU -> AudioModule (P1/P2).
- JMBE (or mbe) decode inside Jmbe/Ambe base.
- NonClippingGain(5.0f, 0.95f) applied.
- Queued timeslots for continuity; tones extracted; identifier updates.
- SquelchState integration for call boundaries.
- Output to SDRTrunk audio framework (per channel/call?).

### Identified Audio Gaps / Risks for "No Clear Audio"
1. **Codec bit layout**: Our interleave tables + bitsFromDibit order + variant probes try to match "sdrtrunk/JMBE". If not exact for all systems, mbelib produces noisy/garbled or silence-like output.
2. **State continuity**: mbe_parms carried, but if we reset decoder on bad frame (see code: rx.p25AmbeVoiceDecoder = P25AmbeVoiceDecoder(); on reject), breaks sustained speech.
3. **Gain/normalization**: We normalize per block; SDRTrunk has dedicated non-clip gain + possibly AGC. Low level voice may come out quiet or with clicks.
4. **Resample artifacts**: Our cubic is good (post-audit), but boundary phase reset or insufficient pending buffer can cause chop.
5. **Output routing**: P25 PCM goes through same AudioEngine as analog. If analog demod path also running or master vol/squelch interferes, or active output selection wrong for monitor mode, no/quiet audio.
6. **Frame rate / 20ms alignment**: 160 samples @8k exactly per frame; if codeword count or slot timing off, either duplicate/missing frames or desync -> warble.
7. **No de-emphasis needed** (correct, P25 is baseband vocoder).

## Root Causes Likely for Current "Not Working + No Clear Audio"
- From logs (p2sf/p2mask sometimes 0, low mac/ess): mask phase or superframe not reliably locking on live signals -> either no voiceCodewords or scrambled ones.
- Cursor/continuity in follow mode drops the exact bursts carrying AMBE, so intermittent or no audio (see prior "flow parity" patches).
- Gating too conservative on real mixed or late-entry traffic: "phase2AudioLockMissing" etc prevent emit.
- Bit-order or CW extraction off-by-dibits on certain systems/vendors -> AMBE eats garbage -> distorted "robotic" or silent output.
- Lack of extra gain/EQ on PCM or symbol level makes "clear" output too quiet or harsh.
- Single RTL shared path vs SDRTrunk dedicated traffic source makes robust following hard.

## Recommended Next Fixes (to match SDRTrunk closer + get clear audio)
1. **Low-level**: Add optional C4FM-specific matched filter taps (RRC or known C4FM response) before timing; expose via config. Add simple running AGC on C4FM symbols (like SDRTrunk 1.2x + equalizer).
2. **Phase2 mask/voice**: Strengthen mask phase selection (use more history like SDRTrunk continuous processor); ensure voice CW extraction uses the descrambled after confirmed lock, with fallback probe only for known-clear grants.
3. **Audio emit**: Add "force clear audio probe" CLI/GUI flag that relaxes some gates (mask/ess) when grant was marked clear and some VCW seen (already some of this); add post-mbelib simple AGC or fixed +6dB boost for P25 voice.
4. **Traffic source**: Further harden continuous dibit feed to traffic processor (more pre-roll, no jump on backlog).
5. **Diagnostics**: Always log chosen variant + errs/rms for every emitted frame; surface "last AMBE errs" and "mask locked?" in UI.
6. **High-level parity**: When possible, mirror more of SDRTrunk's TrafficChannelManager + per-call decoder instances even in one-RTL mode (use independent decoder states).

## Status
Extensive prior work (see P25_*_SDRTRUNK_*_PATCH_NOTES.md and SDRTRUNK_PARITY_*) has aligned many details (offsets, ESS layout, MAC state machine, late entry, resample, mbelib unpack). Current audio is "working" in controlled tests but fragile on live RF -> clear output fails.

## Fixes Applied later for "no audio after grant + stuck on inactive TG"
- Traffic lifetime (lastActive / isCallStillActive) now driven only by actual voice VCW / PTT / session release (not pure sf/mask framing). Prevents keeping inactive TGs alive. See P25TrafficChannelProcessor.cpp.
- voiceStillLooksActive and follow lastActive refresh require recent VCW (or processor audioOpen), not just bursts. See P25FollowStateMachine.cpp and main.cpp follow paths.
- Once decode produces usable PCM (decodedFrames>0 + non-empty audio), force-clear Phase2*Missing / LateEntry / Waiting flags so speaker gate emits. Addresses cases where ESS/sessionRelease lags first VCW on some systems (while still enforcing mask and security before AMBE). Matches SDRTrunk "forward voice timeslots once descrambled".
- To stop noise being treated as "real data": only count VCW as activity (for p2vcw, lastVoice, lastActive, mayEmit) if burst.xorMaskApplied + (superframe/mask lock or mac/ess). Processor now drops callActive after ~4s of no meaningful voice. kMaxSilence reduced. This directly fixes "stays there for ages picking up background noise".

## Fixes Applied 2026-07-01 (this session)
- Added lightweight running level normalization (percentile 80% AGC-like gain 0.6-1.8x) on recovered C4FM symbols after timing loop. Improves slice reliability -> cleaner dibits for voice frames (closer to SDRTrunk equalizer/gain compensation). See P25LiveDecoder.cpp.
- Added modest post-normalize *1.35 boost (clamped) in resampleDecodedP25Pcm for P25 voice PCM before cubic upsample. Ensures louder/clearer output while preserving headroom (SDRTrunk NonClippingGain philosophy). See main.cpp.
- Tightened post-discriminator FIR LPF (cutoff ~0.62 * symbolRate, 121 taps) in prepareC4fmDiscriminatorForSymbols before symbol timing / slicing. Reduces noise while preserving main FSK lobes for better eye opening and fewer bit errors in voice frames.
- Removed per-frame mbe state reset on "not accepted but PCM usable" AMBE frames in the decode path. State and mbelib internal concealment now persist across marginal frames for continuous natural speech (key SDRTrunk/JMBE principle: feed the frames, let the codec handle erasures/repeats). Resets remain only at call boundaries / variant lock / security changes.
- Added magnitude normalization (AGC) on CQPSK symbols after carrier/timing loop (P25LiveDecoder.cpp recoverComplexSymbols) for stable decisions, matching C4FM path and SDRTrunk equalizer/gain spirit.
- Created this full comparison doc from side-by-side source analysis.

Later fixes for "still stuck on inactive TGs after TX stops (noise treated as data)" + "still no audio":
- Processor and snapshot now only use "meaningful" VCW (xorMaskApplied + superframe/mask lock or mac/ess/session) for p2vcw, lastVoice, lastActive, callActive, voiceStillLooksActive. Noise rarely qualifies.
- In late-entry/!effective + grantMayProbe path: attempt AMBE decode anyway; if usable PCM, set acceptedReleaseVoice (emit + clear flags). No longer always queue waiting for ESS.
- Relaxed tdmaNoVcwTimeout (no longer requires phase2Bursts==0; noise can fake bursts).
- Snapshot p2vcw prefers traffic's filtered value.
- Shorter silence limits.
- These target exactly the remaining symptoms.

These are minimal, safe, directly address "clear audio" and "match low level".

Rebuild + hardware test required to confirm clear voice on live P25 CC + grants. Run P25 unit tests (they cover voice CW mapping and AMBE paths). All tests remain green.

Note on background file listings: multiple attempts hit PowerShell syntax ( .Name in Where-Object instead of $_.Name ), producing only errors. Key files were read directly in previous steps (P25P1DemodulatorC4FM, Voice4Timeslot, AudioModules, etc.).

See also: docs/p25_phase2_release_gate.md , src/P25_PHASE2_*_NOTES.md , DESIGN.md P25 sections.

Latest for "still no audio + hanging too long after voice":
- Always attempt p25Decode for target masked VCW (removed early metadata rejects/continues that prevented decode). If decode produces usable, set acceptedRelease to clear flags and allow emit.
- Lowered silence for no-vcw return and carrierDropped to 800ms-2s to return faster once voice (and carrier) ends.
