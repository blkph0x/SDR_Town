#pragma once

// Purpose: P25 Phase 2 voice timing constants, LO park helper, and chunk planner.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 1 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include <QtGlobal>

#include <cmath>
#include <cstddef>
#include <cstdint>

inline bool p25SpeakerNeedsStartupPrime(size_t queuedSamples, size_t pendingSamples,
                                        size_t minimumPrimeSamples) noexcept
{
    // Once playing, each ready frame extends the existing stream immediately.
    return queuedSamples == 0 && pendingSamples < minimumPrimeSamples;
}

// Speaker mute only — decode arms immediately.  Capture 20260712_021852 still
// showed post-arm-settle-muted on the first usable DSP pass; keep mute short so
// clear PCM can emit inside the PTT window once traffic-side proof is available.
inline constexpr qint64 kP25RetunePreArmMuteMs = 80;
inline constexpr int kP25RetunePreArmDiscardWindows = 0;
inline constexpr qint64 kP25Phase1PostArmSettleMs = 250;
inline constexpr qint64 kP25Phase2PostArmSettleMs = 80;
inline constexpr int kP25Phase1PostArmDiscardWindows = 1;
inline constexpr int kP25Phase2PostArmDiscardWindows = 0;
inline constexpr int kP25Phase1ArmDelayMs = 250;
inline constexpr int kP25Phase2ArmDelayMs = 0;
inline constexpr qint64 kP25Phase2SlotProbeSettleMs = 200;
inline constexpr int kP25Phase2SlotProbeDiscardWindows = 1;
// Keep live control-channel decode bounded.  The control TSDU cadence is short
// enough that 256 ms windows still carry useful grant/identifier frames; larger
// 384+ ms windows were taking >2 s to decode on RTL captures and missing the
// repeat of real grants before follow could safely arm.
inline constexpr double kP25ControlDecodeWindowSeconds = 0.256;
inline constexpr int kP25ControlDecodeCadenceMs = 70;
// Keep the Phase 2 traffic decode path aligned with TDMA superframe reality.
// SDRTrunk keeps per-timeslot state alive and queues voice until PTT/ESS proves
// clear/encrypted state. Our rolling replay has to preserve comparable context:
// one 360 ms superframe was enough for VCW bursts but not always enough for
// adjacent ACCH/ESS recovery on real captures. Two superframes keep MAC/ESS and
// voice in view while absolute-dibit de-duplication prevents repeated PCM.
inline constexpr double kP25Phase2VoiceDecodeWindowSeconds = 0.720;
// Keep ≥4 s of traffic IQ while the speaker is live so catch-up can drain lag
// without hard-cap skipping unprocessed voice RF. Capture 20260908_095936 hit
// rolling 2.6–2.9 s against a 2 s window; soft-trim then clipped DEC-0009
// overlap and hard-cap jumped the decode cursor (DEC-0023).
inline constexpr double kP25Phase2VoiceDecodeActiveRollingSeconds = 4.000;
// DEC-0023 raised active rolling to 4.0 s, but live clamps still used
// 4194304 samples (= 2.048 s @ 2.048 MHz). Capture 20260909_060036: after a
// perfect cold emit (TG 10301 emit=32 duty 0.639) rolling stuck at exactly
// 4194304 while worker-busy; subsequent hops p2vcw=0 / drop A. File voicetest
// of the same IQ: PASS_CONTINUOUS_AUDIO duty=0.705. Soft-trim must be allowed
// to keep the full 4.0 s window DEC-0023 named.
inline constexpr double kP25Phase2VoiceDecodeRollingHardCapSeconds = 16.000; // 4× active (DEC-0023 emergency)

inline constexpr double kP25Phase2VoiceDecodeMinFreshSeconds = 0.020;
inline constexpr double kP25Phase2VoiceDecodeAcquireChunkSeconds = 0.050;
// First post-retune eye keeps two superframes of traffic context so late-entry
// Phase-2 calls can recover the TDMA lattice, mask phase, and ESS/MAC side
// information before the short-hop live stream takes over. Worker time is kept
// bounded by the cold realtime budget/candidate caps below, not by throwing
// away this initial context.
inline constexpr double kP25Phase2VoiceDecodeFirstColdEyeSeconds = 0.720;
inline constexpr double kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds = 0.160;
inline constexpr double kP25Phase2VoiceDecodeUnacquiredAcquireMinFreshSeconds = 0.120;
inline constexpr double kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds =
    kP25Phase2VoiceDecodeWindowSeconds - kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds;
inline constexpr int kP25VoiceWorkerDspMutexWaitMs = 80;
// Capture 20260712_021852: worker job seq=2 ran ~2.7s then decode-wall-timeout
// while soft-AFC cold passes that finished in ~80ms were starved.  Cap cold
// wall hard; never boost CQPSK budget up to the wall (see voice worker).
//
// Live vs replay: replay IQ is already on disk so a long walk is stable.
// Live one-RTL must stay near the tuner clock. 3900X/32GB can frame a full
// 12-burst superframe in one locked hop; do not stop after Voice4 #1.
inline constexpr int kP25VoiceWorkerMaxDecodeWallMs = 320;
inline constexpr int kP25VoiceWorkerColdDecodeWallMs = 400;
inline constexpr int kP25VoiceWorkerColdRealtimeBudgetMs = 240;
inline constexpr size_t kP25VoiceWorkerColdMaxCqpskCandidates = 64;
// Live hot search (no sticky Costas yet / block-channelize fallback).
inline constexpr int kP25VoiceWorkerHotRealtimeBudgetMs = 120;
inline constexpr size_t kP25VoiceWorkerHotMaxCqpskCandidates = 8;
inline constexpr size_t kP25VoiceWorkerHotMaxPhase2SyncHits = 96;
inline constexpr size_t kP25VoiceWorkerHotMaxPhase2SuperframeLocks = 12;
// Live locked streaming DDC: one sticky CQPSK candidate, walk a superframe.
inline constexpr int kP25LiveLockedStreamBudgetMs = 120;
inline constexpr size_t kP25LiveLockedStreamCqpskCandidates = 1;
inline constexpr size_t kP25LiveLockedStreamSyncHits = 128;
inline constexpr size_t kP25LiveLockedStreamSuperframeLocks = 12;
// File replay / voicetest: no live tuner; 3900X can walk more RF per hop.
inline constexpr int kP25ReplayHotBudgetMs = 240;
inline constexpr size_t kP25ReplayHotCqpskCandidates = 16;
inline constexpr size_t kP25ReplayHotSyncHits = 160;
inline constexpr size_t kP25ReplayHotSuperframeLocks = 16;
// DEC-0041 / capture 20260911_234224: live GUI eye-lost re-lock keeps replay
// candidate width (16) but must not inherit the 240 ms CLI/voicetest wall —
// that produced dsp p90 ~461 ms / job gaps ~800 ms and drop D under
// single-flight. Live escalate uses the same 120 ms hot wall as DEC-0019.
inline constexpr int kP25LiveEyeLostReplayBudgetMs = kP25VoiceWorkerHotRealtimeBudgetMs;
// First post-emit eye-lost hop stays on hot cand=8; escalate cand=16 at streak.
// DEC-0048 / capture 20260912_041612: file voicetest TG20202 duty 0.805 while
// live CADENCE max 0.399 then permanent no-vcw + worker-busy. Waiting for
// streak≥2 left one more empty hop under cand=8 before widen — escalate on
// the first post-emit eye-lost hop (streak≥1).
inline constexpr int kP25LiveEyeLostReplayCandStreak = 1;
// DEC-0042 / capture 20260912_002128: healthy post-emit eyes still ran
// emit-gate dsp p50≈199 ms (budget 120) → worker-busy on every TG while the
// *same* IQ file voicetest hits duty≥0.83. Tighten only the healthy sustain
// path (eye present); keep DEC-0041 eye-lost escalate width.
inline constexpr int kP25LiveHealthySustainBudgetMs = 80;
inline constexpr size_t kP25LiveHealthySustainCqpskCandidates = 4;
// DEC-0054 (081416 no-audio): cold first-eye must NOT forceCheap — re-arm a
// generous full-annotate window. Sticky sustain may cheap-commit with hot budget.
inline constexpr int kP25LiveColdCommitAllowanceMs = 200;
inline constexpr int kP25LiveStickyCheapCommitAllowanceMs = 120;
// Legacy alias (DEC-0053); sticky path uses StickyCheap above.
inline constexpr int kP25LiveCheapCommitAllowanceMs = kP25LiveStickyCheapCommitAllowanceMs;
// DEC-0045 proposed clamping decode wall to 105/145 near these budgets.
// DEC-0046 REJECTED that: wall is post-hoc (no cooperative abort). Empty
// eyes finishing >105 ms were stamped decode-wall-timeout and wiped speaker
// pending → continuous audio death. Keep global wall 320; never clear
// pending on wall stamps alone (see p25Phase2WallTimeoutMayClearSpeakerPending).
// DEC-0051 adds cooperative mid-decode abort inside processIq — wall stays
// 320; do not revive DEC-0045 clamps.
inline constexpr int kP25LiveHealthySustainWallMs = kP25VoiceWorkerMaxDecodeWallMs;
inline constexpr int kP25LiveEyeLostSustainWallMs = kP25VoiceWorkerMaxDecodeWallMs;
// DEC-0044: auto PPM from sustained CC AFC (never mid-voice).
// DEC-0049 / capture 20260912_044651: AFC=1250Hz conf=0.45 (floor) stepped
// -2.97 ppm twice (−1.93→−7.88) while summary residual was ~874 Hz and CC
// TSBK dibit corrections climbed. Tighten gates + prefer trusted offset.
inline constexpr double kP25AutoPpmMinAbsAfcHz = 200.0;
inline constexpr double kP25AutoPpmMaxAbsAfcHz = 2000.0; // was 3500; reject soft-probe rails
inline constexpr double kP25AutoPpmMinAbsDelta = 0.40;
inline constexpr double kP25AutoPpmMaxStep = 1.50; // was 5.0 — one gentle step per apply
inline constexpr double kP25AutoPpmMinConfidence = 0.55; // match CLI ppm apply
inline constexpr qint64 kP25AutoPpmCooldownMs = 120000; // was 30s — let CC re-lock
// Trusted CC offset may be older than carry-fresh (30s) after a long PTT;
// still bake PPM from it if within this window and magnitude gates pass.
inline constexpr qint64 kP25AutoPpmTrustedOffsetMaxAgeMs = 300000;
// Refuse auto-apply when |AFC| looks like a soft-probe rail (±1250 class).
inline constexpr double kP25AutoPpmSoftProbeRailHz = 1250.0;
inline constexpr double kP25AutoPpmSoftProbeRailTolHz = 5.0;

// Pure gate used by auto-PPM and Catch (DEC-0049).
inline bool p25AutoPpmAfcSampleAcceptable(double afcOffsetHz, double afcConfidence) noexcept
{
    if (!std::isfinite(afcOffsetHz) || !std::isfinite(afcConfidence)) return false;
    const double absAfc = std::abs(afcOffsetHz);
    if (absAfc < kP25AutoPpmMinAbsAfcHz || absAfc > kP25AutoPpmMaxAbsAfcHz) return false;
    if (afcConfidence < kP25AutoPpmMinConfidence) return false;
    if (std::abs(absAfc - kP25AutoPpmSoftProbeRailHz) <= kP25AutoPpmSoftProbeRailTolHz) {
        return false;
    }
    return true;
}
inline constexpr double kP25Phase2VoiceDecodeAcquireOverlapSeconds = 0.160;
// Sustain: stream like SDRTrunk SuperFrameDetector — frequent short advances
// along a locked superframe lattice, not one giant re-lock every 1.5s.
// Field 20260712_125240: emit of exactly 4 AMBE frames every ~1.5–2.5s (80 ms
// audio blocks) because workers processed too little RF then idled.  Keep one
// 360 ms superframe of context (12 × 30 ms bursts) and advance 80 ms fresh.
// Capture 20260905_105622 after DEC-0008: 80 ms overlap (160 ms eyes) often
// p2bursts=1 and duty 0.40/0.51; hop-2 720 ms eyes recovered 24 slot-0 VCWs.
// 280+80 = 360 ms (DEC-0009). Do not replay 720 ms every tick.
// DEC-0017 tried chunk=minFresh=0.360 overlap=0 (1x DSP). Voicetest 105622
// skip=97334 fell duty 0.685→0.35; 123525 skip=20000 fell 0.64→0.325.
// Independent 360 ms eyes miss the DEC-0007 overlap catch-up. Reverted.
inline constexpr double kP25Phase2VoiceDecodeSustainChunkSeconds = 0.080;
inline constexpr double kP25Phase2VoiceDecodeSustainMinFreshSeconds = 0.040;
inline constexpr double kP25Phase2VoiceDecodeSustainOverlapSeconds = 0.280;
// Block-channelize speaker hops use the same locked-sustain geometry as the
// non-speaker path.  The first cold eye above still supplies two-superframe
// late-entry context; once audio/acquire state is active, replaying 720 ms of
// fresh RF per tick makes the worker fall behind live traffic and the rolling
// buffer eventually skips real voice frames.
inline constexpr double kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080;
inline constexpr double kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.040;
inline constexpr double kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.280;
// Streaming-DDC live traffic: contiguous slices like SDRTrunk HDQPSK
// `receive`. SDRTrunk's own HDQPSK `main()` consumes 2048 complex samples at
// 25 kHz ≈ 81.92 ms. Capture 20260830_064319: 40 ms context-free hops lost
// the CQPSK eye (p2bursts=0). Locked hops stay 80 ms; minFresh 40 ms is two
// 20 ms AMBE frames (DEC-0014). DEC-0022 tried 160 ms sustain (105622 stream
// duty 0.01→0.125) — still far below 0.65; reverted.
inline constexpr double kP25Phase2StreamingLiveSliceSeconds = 0.080;
inline constexpr double kP25Phase2StreamingLiveMinFreshSeconds = 0.040;
// ISS-0003 / 20260903: removed dead 180/100/100 speaker live-edge catch-up
// constants. Live planner must not skip to live-edge with 180 ms catch-up on
// the speaker path (see p25Phase2PlanVoiceDecodeChunk: speaker-sustain 80+280,
// then backlogCatchUp).
// If the voice worker falls behind live RF, decode a larger near-live chunk
// so one worker pass can refill the speaker ring. Cap fresh at 120 ms so a
// single job cannot monopolize the worker before hard clear.
// DEC-0024 / capture 20260908_101644: catch-up overlap was 40 ms
// (context=81920). After the cold eye the planner spent ~8 s on 120 ms eyes
// (fresh~80 + overlap 40), CADENCE dutySec=0 then drop A / no voice sync.
// Late TG 10330 on the same capture stayed on DEC-0009 context=573440 and
// sounded ~90% — SDRTrunk HDQPSK never shrinks traffic context to chase lag.
// Keep catch-up fresh aggressive; overlap must stay DEC-0009 280 ms.
inline constexpr double kP25Phase2VoiceDecodeBacklogCatchUpChunkSeconds = 0.120;
inline constexpr double kP25Phase2VoiceDecodeBacklogCatchUpMinFreshSeconds = 0.080;
inline constexpr double kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds = 0.280;
// DEC-0058 / capture 20260912_142104 TG10301: speaker-sustain stayed on
// 80+280 while dsp≈200–600 ms → emit gaps. DEC-0058 first tried 160+280.
// DEC-0060 / 152348 thinned overlap to 80 ms (280+80) to pace RF — that
// violated DEC-0024. Capture 20260912_153932 after 0060: WAV islands p50=40 ms,
// chop pairs=76, bridge top-ups 68/69 at ringFill~11%, workers dominant
// 160+80/280+80 with empty-audio majority. DEC-0061 restores 280 ms overlap
// and advances 240 ms fresh (≥ emit wall~200–230 ms / dsp p50~220) so pace
// and lock both hold. Idle sustain stays 80+280.
inline constexpr double kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds = 0.240;
inline constexpr double kP25Phase2VoiceDecodeSpeakerBacklogCatchUpMinFreshSeconds = 0.160;
inline constexpr double kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds = 0.280;
inline constexpr double kP25Phase2VoicePullWindowSeconds = 0.100;
inline constexpr int kP25Phase2VoiceDecodeCadenceMs = 10;
inline constexpr int kP25Phase2VoiceDecodeColdCadenceMs = 8;
inline constexpr int kP25Phase2VoiceDecodeSpeakerCadenceMs = 3;
inline constexpr int kP25Phase2VoiceDecodeMaxCadenceMs = 40;
// Keep Phase-2 voice decode single-flight.  The decoder, security latch,
// absolute VCW de-dupe, and mbelib state are all call/slot stateful; queued
// near-live windows become stale audio islands when they publish after the
// traffic state has moved on.  The capacity check below counts the running job,
// queued jobs, and unpublished completed results.
inline constexpr size_t kP25VoiceDecodeMaxPendingJobs = 1;
inline constexpr size_t kP25VoiceDecodeMaxPendingJobsSpeaker = 1;
inline constexpr size_t kP25VoiceDecodeMaxCompletedResults = 16;
// Keep the one-RTL Phase-2 target-offset recovery bounded.  Field logs after
// the slot/queue fixes showed DSP passes taking 440ms while only emitting 40ms
// of audio; most of that cost came from cloning the live decoder through several
// offset candidates after the traffic channel was already producing target-slot
// VCWs.  Offset probing is acquisition-only now, not a sustain-path tax.
inline constexpr int kP25Phase2TrafficOffsetProbeBudgetMs = 70;
// Cold acquire: prefer several short jobs (soft-AFC → ±1250 → ±2500) over one
// 320ms/4-candidate monolith that produces worker-busy gaps over short PTTs.
inline constexpr int kP25Phase2TrafficOffsetProbeAcquireBudgetMs = 150;
inline constexpr size_t kP25Phase2TrafficOffsetProbeMaxCandidates = 2;
inline constexpr size_t kP25Phase2TrafficOffsetProbeAcquireMaxCandidates = 2;
// DEC-0015: do not park the LO on the voice carrier (RTL DC/LO spike) and do
// not invent a 250 kHz offset. SDRTrunk CenterFrequencyCalculator puts a
// single 12.5 kHz channel just right of the R820T 5 kHz DC hole
// (voice − 11249 Hz). See include/P25SdrtrunkTune.h.
inline constexpr double kP25Phase2WidebandTrafficPreRollSeconds = 0.420;
inline constexpr double kP25Phase2PhysicalRetunePreRollSeconds = 0.180;
inline constexpr qint64 kP25AutoFollowDifferentCallMinDwellMs = 12000;
// Scanner-follow cannot decode both same-RF TDMA slots the way sdrtrunk's
// traffic-channel manager can.  Do not immediately steal from a just-tuned
// slot before the arm+settle+first-acquisition window has had any chance to
// see PTT/ESS/voice.  The field log showed TG30302 slot 1 being replaced by
// TG30003 slot 0 in the same millisecond, long before DSP was armed.
inline constexpr qint64 kP25Phase2SameRfSlotHandoffGraceMs = 8000;
// Same-RF Phase 2 grants can describe both TDMA slots on one RF carrier.  SDRTrunk
// keeps two traffic processors alive; this GUI currently has one selected audio
// target, so a slow-to-acquire slot must not be stolen just because the companion
// slot advertised an OP=0x02 update.  Field replay showed clear audio arriving
// about 16 s after the first grant, so require a longer no-audio dwell before
// changing selected slot without proven silence.
inline constexpr qint64 kP25Phase2SameRfUnacquiredSlotStealMs = 24000;
inline constexpr qint64 kP25Phase2SameRfMetadataSwitchCooldownMs = 8000;
inline constexpr qint64 kP25Phase2SameRfClearGrantHoldMs = 12000;
// Short hold: playout bridge / hop jitter / cadence diag only.
inline constexpr qint64 kP25Phase2SpeakerFollowHoldMs = 2500;
// Capture 20260808_032428: different-TG steal at +0.5s while still emitting
// (currentFollowSpeakerActive used only 2.5s). Protect active clear speech
// against any different-TG auto-follow steal for a full inter-island gap.
inline constexpr qint64 kP25Phase2SpeakerFollowProtectMs = 20000;
// Only treat very recent speaker output as a decode cadence gap.
inline constexpr qint64 kP25Phase2SpeakerDecodeGapBlockMs = 2500;

inline constexpr qint64 kP25Phase2WarmStandbyMs = 5000;
inline constexpr qint64 kP25Phase2UnacquiredDwellStealGraceMs = 900;
inline constexpr qint64 kP25Phase2ClearTrustedUnacquiredDwellStealGraceMs = 15000;
inline constexpr qint64 kP25Phase2SilentDwellStealGraceMs = 3500;
inline constexpr qint64 kP25Phase2ClearTrustedSilentDwellStealGraceMs = 18000;
inline constexpr int kP25Phase2TrafficTargetOffsetVerifiedTrust = 2;
inline constexpr bool kP25Phase2TrafficTargetOffsetProbeEnabled = true;
// One-RTL traffic is already centred on the granted voice channel; offsets beyond
// a narrow AFC window are almost always stale cross-channel locks or image hits.
inline constexpr double kP25Phase2TrafficTargetOffsetMaxHz = 7500.0;
inline constexpr double kP25Phase2ControlCarryOffsetMinHz = 50.0;
inline constexpr double kP25Phase2ControlCarryOffsetMaxHz = 3500.0;
inline constexpr qint64 kP25Phase2ControlCarryFreshMs = 30000;

size_t p25Phase2VoiceRollingMaxSamples(double sampleRateHz, double windowSeconds) noexcept;

double p25Phase2LowIfTrafficCenterHz(double voiceHz,
                                    double sampleRateHz,
                                    double companionHz = 0.0) noexcept;

size_t p25Phase2TrafficPreRollSamples(double sampleRateHz, bool physicalRetune) noexcept;
qint64 p25PostArmSettleMs(bool phase2) noexcept;
int p25PostArmDiscardWindows(bool phase2) noexcept;

// Shared GUI/CLI voice-chunk plan.  Field 20260801_100006 showed 417×
// waiting-fresh-iq with minFresh==rolling (720 ms) after the first job advanced
// the decode cursor — that is not how SDRTrunk streams traffic timeslots.
struct P25Phase2VoiceChunkPlan {
    double maxChunkSeconds = 0.040;
    double overlapSeconds = 0.020;
    double minFreshSeconds = 0.020;
    double minFreshFloorSamples = 8192.0;
    bool treatAsContextFreeFresh = false;
};

size_t p25Phase2EffectiveMinFreshSamples(size_t rollingSamples,
                                              size_t decodeOverlap,
                                              size_t minDecodeFreshNominal,
                                              size_t minDecodeFreshFloor,
                                              size_t maxDecodeChunk = 0);

P25Phase2VoiceChunkPlan p25Phase2PlanVoiceDecodeChunk(
    bool streamingDdc,
    bool backlogCatchUp,
    bool activeSpeakerClearPath,
    bool wideReacquireWindow,
    bool maskEpochRepairWindow,
    bool speakerSustainDecode,
    bool phase2SustainDecodeWindow,
    bool firstColdEyeChunk,
    bool unacquiredAcquireWindow,
    bool decodeCursorAdvancedPastStart) noexcept;
