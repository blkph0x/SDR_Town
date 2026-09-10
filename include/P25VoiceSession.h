#pragma once

// Purpose: P25 Phase 2 session / sustain / cadence / streaming orchestration helpers.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase A (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "P25AppGlobals.h"
#include "P25VoiceDecode.h"
#include "P25VoiceTiming.h"
#include "Receiver.h"

#include <QtGlobal>

#include <cstddef>

// Phase-2 late entry on real systems often reaches the traffic channel after
// PTT/ESS has already passed.  Keep the sdrtrunk-style queue as the primary
// path. Target MAC/ESS proves clear audio; an explicit clear control-channel
// grant may also release after target-slot hard voice, XOR mask, and diagnostic
// AMBE validation prove the audio path is sane. Explicit encrypted grants/ESS
// remain fail-closed.
// Capture 20260712_021852: unknown grants spent 700 ms queued while short PTTs
// ended; clear-known releases still need a small grace, but keep it tight.
inline constexpr qint64 kP25Phase2AudioTailGraceMs = 100;
inline constexpr qint64 kP25Phase2SpeakerAudioTailGraceMs = 2500;

qint64 p25Phase2EffectiveAudioTailGraceMs() noexcept;
bool p25Phase2SessionHasHardTargetAcquire(const Receiver& rx) noexcept;
bool p25Phase2SessionHadVoiceLock(const Receiver& rx) noexcept;
bool p25Phase2SessionHadBurstEye(const Receiver& rx) noexcept;
bool p25Phase2SessionSpeakerSustainActive(const Receiver& rx) noexcept;
bool p25Phase2EstablishedClearVoiceStreamingLocked(const Receiver& rx) noexcept;
double p25Phase2EffectiveRollingWindowSeconds(const Receiver& rx) noexcept;
bool p25TrustedControlOffsetForPhase2Traffic(double controlFreqHz, qint64 nowMs, double* outOffsetHz) noexcept;
void p25SeedPhase2TrafficOffsetFromControl(Receiver& rx, double offsetHz, int trust = 1) noexcept;
int p25Phase2AdaptiveVoiceDecodeCadenceMs() noexcept;
int p25Phase2AdaptiveVoiceDecodeCadenceMs(const Receiver& rx) noexcept;
bool p25Phase2SpeakerSustainDecodeActive() noexcept;
size_t p25VoiceDecodeMaxPendingJobsNow(bool speakerSustainHint) noexcept;
size_t p25VoiceDecodeMaxPendingJobsNow() noexcept;
bool p25Phase2HasStableSuperframeLockLocked(const Receiver& rx) noexcept;
bool p25Phase2NeedsWideReacquireWindowLocked(const Receiver& rx) noexcept;
bool p25Phase2UseSustainDecodeWindowLocked(const Receiver& rx) noexcept;
int p25Phase2StreamingDdcEnvOverride() noexcept;
bool p25Phase2StreamingDdcExperimentEnabled();
