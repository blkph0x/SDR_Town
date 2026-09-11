#include "MainWindow.h"

#include <QTimer>

// AUTOMOC: Q_OBJECT lives in MainWindow.h (SpectrumWidget / TranscriptWindow pattern).
// P25 clear-audio voice worker / submit / backpressure / publish (ISS-0004 follow-up).
// Mechanical move from MainWindow.cpp — no behavior change.

void MainWindow::startP25VoiceWorker()
{
        if (p25VoiceWorkerThread.joinable()) return;
        p25VoiceWorkerStop.store(false, std::memory_order_release);
        p25VoiceWorkerThread = std::thread([this]() {
            for (;;) {
                MainWindow::P25VoiceDecodeJob job;
                {
                    std::unique_lock<std::mutex> lock(p25VoiceWorkerMutex);
                    p25VoiceWorkerCv.wait(lock, [this]() {
                        return p25VoiceWorkerStop.load(std::memory_order_acquire) ||
                               !p25VoicePendingJobs.empty();
                    });
                    if (p25VoiceWorkerStop.load(std::memory_order_acquire) && p25VoicePendingJobs.empty()) {
                        break;
                    }
                    job = std::move(p25VoicePendingJobs.front());
                    p25VoicePendingJobs.pop_front();
                    p25VoiceWorkerBusy.store(true, std::memory_order_release);
                }

                MainWindow::P25VoiceDecodeResult result;
                result.rx = job.rx;
                result.audioOutputIndices = job.audioOutputIndices;
                result.sampleRateHz = job.sampleRateHz;
                result.centerFreqHz = job.centerFreqHz;
                result.targetFreqHz = job.targetFreqHz;
                result.outputRateHz = job.outputRateHz;
                result.iqStartAbsolute = job.iqStartAbsolute;
                result.iqStartAbsoluteKnown = job.iqStartAbsoluteKnown;
                result.iqDecodeEndAbsolute = job.iqDecodeEndAbsolute;
                result.iqDecodeEndAbsoluteKnown = job.iqDecodeEndAbsoluteKnown;
                result.outputMutedForSettle = job.outputMutedForSettle;
                result.rollingDecode = job.rollingDecode;
                result.iqSamples = job.iq.size();
                result.freshIqSamples = job.freshIqSamples;
                result.contextIqSamples = job.contextIqSamples;
                result.trafficGeneration = job.trafficGeneration;
                result.talkgroupId = job.talkgroupId;
                result.sourceId = job.sourceId;
                result.tdmaSlotKnown = job.tdmaSlotKnown;
                result.tdmaSlot = job.tdmaSlot;
                result.voiceFreqHz = job.voiceFreqHz;
                result.sequence = job.sequence;
                result.flushSeq = job.flushSeq;
                result.receiverSessionKey = job.receiverSessionKey;
                result.callSessionId = job.callSessionId;

                const uintptr_t workerRxKey = reinterpret_cast<uintptr_t>(job.rx.get());
                const uint64_t workerSeq = job.sequence;
                const size_t workerIq = job.iq.size();
                const size_t workerFresh = job.freshIqSamples;
                const size_t workerContext = job.contextIqSamples;
                const bool workerRolling = job.rollingDecode;
                const double workerSr = job.sampleRateHz;
                const double workerCf = job.centerFreqHz;
                const double workerTarget = job.targetFreqHz;
                const uint32_t workerTg = job.talkgroupId;
                const uint32_t workerSource = job.sourceId;
                const bool workerSlotKnown = job.tdmaSlotKnown;
                const int workerSlot = static_cast<int>(job.tdmaSlot & 0x01u);
                const uint64_t workerGeneration = job.trafficGeneration;
                QTimer::singleShot(0, this, [this, workerRxKey, workerSeq, workerIq, workerFresh,
                                              workerContext, workerRolling, workerSr, workerCf,
                                              workerTarget, workerTg, workerSlotKnown, workerSlot,
                                              workerGeneration, workerSource]() {
                    const QString key = QString("p25-voice-worker-start:%1").arg(static_cast<qulonglong>(workerRxKey));
                    appendP25LogLineKeyed(key,
                        QString("P25 DSP VOICE WORKER START: seq=%1 rolling=%2 iq=%3 fresh=%4 context=%5 sr=%6MHz cf=%7MHz target=%8MHz tg=%9 src=%10 slot=%11 generation=%12.")
                            .arg(static_cast<qulonglong>(workerSeq))
                            .arg(workerRolling ? "yes" : "no")
                            .arg(static_cast<qulonglong>(workerIq))
                            .arg(static_cast<qulonglong>(workerFresh))
                            .arg(static_cast<qulonglong>(workerContext))
                            .arg(workerSr / 1e6, 0, 'f', 3)
                            .arg(workerCf / 1e6, 0, 'f', 5)
                            .arg(workerTarget / 1e6, 0, 'f', 5)
                            .arg(workerTg)
                            .arg(workerSource != 0 ? p25HexId(workerSource, 6) : QStringLiteral("unknown"))
                            .arg(workerSlotKnown ? QString::number(workerSlot) : QStringLiteral("unknown"))
                            .arg(static_cast<qulonglong>(workerGeneration)),
                        750);
                });

                auto applyQueuedSlotProbeBeforeDecode = [&](std::string* reason) -> bool {
                    if (!job.rx) return false;
                    Receiver& rx = *job.rx;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.p25VoiceSlotProbePending || !rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) {
                        return false;
                    }

                    // Immutable grant slot: cancel queued probe and keep decoding.
                    if (p25Phase2GrantedSlotIsImmutable(rx)) {
                        ++rx.p25DiagSlotProbeBlocked;
                        rx.p25VoiceSlotProbePending = false;
                        rx.p25VoiceSlotProbeRequested = 0;
                        return false;
                    }

                    // If the current granted slot has produced recent target-slot PCM,
                    // the queued probe is stale.  Cancel it instead of flipping away
                    // from working audio.  This addresses field logs where
                    // slotProbePending=yes stayed set while decoded/audio continued.
                    const auto& diag = rx.p25VoiceDiagnostics;
                    const bool recentUsefulAudio =
                        diag.updatedMs > 0 &&
                        nowMs - diag.updatedMs <= 1800 &&
                        diag.decodedFrames > 0 &&
                        diag.audioSamples > 0 &&
                        diag.phase2TargetVoiceCodewords > 0 &&
                        diag.phase2OppositeVoiceCodewords == 0;
                    if (recentUsefulAudio) {
                        rx.p25VoiceSlotProbePending = false;
                        rx.p25VoiceSlotProbeRequested = 0;
                        return false;
                    }

                    const uint8_t requestedSlot = static_cast<uint8_t>(rx.p25VoiceSlotProbeRequested & 0x01u);
                    {
                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                        if (!dspLock.owns_lock()) {
                            if (reason) {
                                *reason = "slot-probe-pending-dsp-busy";
                            }
                            // Do not decode the old slot while a probe is pending; that
                            // is exactly how stale slot hypotheses leaked random/choppy audio.
                            return true;
                        }
                        if (!applyP25Phase2SlotProbeLocked(rx, requestedSlot, nowMs)) {
                            return false;
                        }
                    }
                    if (reason) {
                        *reason = "slot-probe-applied-before-decode";
                    }
                    return false;
                };

                auto stillCurrent = [&](std::string* reason) -> bool {
                    auto fail = [&](const char* why) {
                        if (reason && reason->empty()) *reason = why ? why : "stale";
                        return false;
                    };
                    if (!job.rx) return fail("no-receiver");
                    if (job.flushSeq != p25PendingAudioFlushSeq.load(std::memory_order_acquire)) {
                        return fail("audio-flush-sequence-stale");
                    }
                    Receiver& rx = *job.rx;
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.active) return fail("receiver-inactive");
                    if (!rx.p25VoiceDecodeEnabled) return fail("voice-decode-disabled");
                    if (!rx.p25VoicePhase2) return fail("not-phase2");
                    if (rx.p25VoiceResetPending) return fail("voice-reset-pending");
                    if (job.callSessionId != 0 &&
                        rx.p25CurrentCallSessionId != 0 &&
                        rx.p25CurrentCallSessionId != job.callSessionId) {
                        return fail("call-session-changed");
                    }
                    const double liveVoiceHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
                    if (job.voiceFreqHz > 0.0 && liveVoiceHz > 0.0 &&
                        std::abs(liveVoiceHz - job.voiceFreqHz) > 50.0) {
                        return fail("voice-frequency-changed");
                    }
                    // The decoder may observe both TDMA slots on one RF carrier,
                    // but the GUI speaker path has one selected call.  Do not
                    // publish a queued result for an old TG/slot just because the
                    // physical voice carrier/generation still matches.
                    if (job.talkgroupId != 0 && rx.p25VoiceTalkgroupId != job.talkgroupId) return fail("talkgroup-changed");
                    // Source/RID is traffic metadata, not a live decode-session
                    // boundary.  The selected Phase-2 audio path is keyed by
                    // TG, call session, grant epoch, slot, and carrier; dropping
                    // worker results solely on RID churn discards queued target
                    // AMBE before ESS/PTT can release it.
                    if (job.tdmaSlotKnown &&
                        (!rx.p25VoiceTdmaSlotKnown ||
                         static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) != static_cast<uint8_t>(job.tdmaSlot & 0x01u))) {
                        return fail("slot-changed");
                    }
                    if (job.trafficGeneration != 0 || job.independentTrafficSource) {
                        const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                        if (job.trafficGeneration == 0 ||
                            rx.p25TrafficGeneration != job.trafficGeneration ||
                            liveGen != job.trafficGeneration) {
                            return fail("traffic-generation-stale");
                        }
                    }
                    return true;
                };

                bool publishResult = false;
                try {
                    std::string staleReason;
                    if (!job.rx) {
                        result.stale = true;
                        result.staleReason = "no-receiver";
                        publishResult = true;
                    } else if (job.iq.empty()) {
                        result.stale = true;
                        result.staleReason = "empty-iq";
                        publishResult = true;
                    } else if (applyQueuedSlotProbeBeforeDecode(&staleReason)) {
                        result.stale = true;
                        result.staleReason = staleReason.empty() ? "slot-probe-applied-before-decode" : staleReason;
                        publishResult = true;
                    } else if (!stillCurrent(&staleReason)) {
                        result.stale = true;
                        result.staleReason = staleReason.empty() ? "stale-before-decode" : staleReason;
                        publishResult = true;
                    } else {
                        Receiver& rx = *job.rx;
                        const auto t0 = std::chrono::steady_clock::now();
                        // Cold only until hard acquire, target VCW, or solid
                        // mask/superframe structure. Ending cold on any p2burst
                        // alone (20260807_235726) drove lock-only/hot budgets
                        // while the eye was still wrong → emit=7 empty=805.
                        // Capture 20260909_053448: structureNoVcw eyes still ran
                        // cold 240/64 (med ~484 ms) because peakTargetVcw stayed
                        // 0 while sf/mask were already present — worker-busy
                        // starved the next live windows (drop D). Exit cold once
                        // sustain has epoch-grade structure (sf+mask >=4), same
                        // bar as tdmaEpochLockedNoMacEss, without inventing TTL.
                        const auto& coldSustain = rx.p25SessionState.sustain;
                        const bool sustainStructureAcquired =
                            coldSustain.peakPhase2MaskedBursts >= 4 &&
                            coldSustain.peakPhase2SuperframeBursts >= 4;
                        const bool coldAcquireJob =
                            rx.p25IndependentTrafficSource &&
                            rx.p25VoicePhase2 &&
                            !p25Phase2SessionHasHardTargetAcquire(rx) &&
                            !sustainStructureAcquired &&
                            coldSustain.peakPhase2TargetVoiceCodewords == 0 &&
                            rx.p25VoiceDiagnostics.phase2TargetVoiceCodewords == 0;
                        const bool establishedClearStreaming =
                            p25Phase2EstablishedClearVoiceStreamingLocked(rx);
                        const P25VoiceDiagSnapshot& workerDiag = rx.p25VoiceDiagnostics;
                        const bool selectedClearStreamingEye =
                            rx.p25VoiceClearKnown &&
                            !rx.p25VoiceEncrypted &&
                            rx.p25VoiceMaskParamsKnown &&
                            rx.p25VoiceTdmaSlotKnown &&
                            (rx.p25SessionState.sustain.peakPhase2TargetVoiceCodewords >= 1 ||
                             workerDiag.phase2TargetVoiceCodewords > 0 ||
                             rx.p25SessionState.sustain.peakPhase2MaskedBursts >= 1 ||
                             workerDiag.phase2MaskedBursts > 0 ||
                             p25DiagTargetHardClear(workerDiag)) &&
                            p25DiagTargetHardClear(workerDiag) &&
                            (p25Phase2SessionHadBurstEye(rx) ||
                             rx.p25VoiceLiveDecoder.cqpskLockValid() ||
                             rx.p25SessionState.sustain.hadBootstrapMaskLock);
                        // Lock-only only when streaming DDC is on AND CQPSK is
                        // actually valid with clear streaming evidence. Never
                        // force candidates=1 from a single false p2burst.
                        // DEC-0021/0022: streaming eye still dies after first
                        // emit on 105622 even without lock-only / with 160 ms
                        // slices — leave this gate in place for future env=1
                        // work; default live stays block (DEC-0014).
                        const bool streamingCqpskJob =
                            rx.p25IndependentTrafficSource &&
                            rx.p25VoicePhase2 &&
                            rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc &&
                            rx.p25VoiceLiveDecoder.cqpskLockValid() &&
                            !coldAcquireJob &&
                            (establishedClearStreaming ||
                             selectedClearStreamingEye ||
                             rx.p25SessionState.sustain.hadSuccessfulEmit);
                        const bool hotPhase2TrafficJob =
                            rx.p25IndependentTrafficSource &&
                            rx.p25VoicePhase2 &&
                            !coldAcquireJob &&
                            (streamingCqpskJob ||
                             p25Phase2SessionHasHardTargetAcquire(rx) ||
                             p25Phase2SessionHadBurstEye(rx) ||
                             rx.p25SessionState.sustain.peakPhase2MaskedBursts >= 1 ||
                             rx.p25SessionState.sustain.peakPhase2SuperframeBursts >= 2);
                        const int decodeWallMs = coldAcquireJob
                            ? kP25VoiceWorkerColdDecodeWallMs
                            : kP25VoiceWorkerMaxDecodeWallMs;
                        const auto dspWaitDeadline = t0 + std::chrono::milliseconds(kP25VoiceWorkerDspMutexWaitMs);
                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::defer_lock);
                        while (!dspLock.try_lock()) {
                            if (std::chrono::steady_clock::now() >= dspWaitDeadline) break;
                            std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                        if (!dspLock.owns_lock()) {
                            result.stale = true;
                            result.staleReason = "dsp-mutex-timeout";
                            publishResult = true;
                        } else {
                            const auto decodeDeadline =
                                std::chrono::steady_clock::now() + std::chrono::milliseconds(decodeWallMs);
                            const int priorDecodeBudgetMs = rx.p25VoiceLiveDecoder.config().realtimeDecodeBudgetMs;
                            const size_t priorCqpskCandidates = rx.p25VoiceLiveDecoder.config().maxCqpskSearchCandidates;
                            const size_t priorPhase2SyncHits = rx.p25VoiceLiveDecoder.config().maxPhase2SyncHits;
                            const size_t priorPhase2Locks = rx.p25VoiceLiveDecoder.config().maxPhase2SuperframeLocks;
                            auto boundedConfigValue = [](size_t current, size_t cap) {
                                return current == 0 ? cap : std::min(current, cap);
                            };
                            // Never boost CQPSK budget to the wall timeout — that
                            // turned cold acquire into multi-second monoliths
                            // (20260712_021852 seq=2 decode-wall-timeout ~2.7s).
                            // Cap cold jobs instead so soft-AFC / ±1250 probes fit
                            // several attempts inside one short PTT.
                            // Once CQPSK is soft-locked, force lock-only streaming
                            // (SDRTrunk continuous demod) instead of re-searching.
                            if (coldAcquireJob) {
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25VoiceWorkerColdRealtimeBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    std::min(priorCqpskCandidates, kP25VoiceWorkerColdMaxCqpskCandidates));
                            } else if (streamingCqpskJob) {
                                // Live locked stream: one Costas eye, walk every
                                // timeslot in the hop. Candidates=1 is required
                                // (0 = unlimited grid). Do not cap SF locks at 1
                                // — that was the 80 ms Voice4 chip.
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25LiveLockedStreamBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    kP25LiveLockedStreamCqpskCandidates);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                                    kP25LiveLockedStreamSyncHits);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                                    kP25LiveLockedStreamSuperframeLocks);
                            } else if (hotPhase2TrafficJob &&
                                       rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc) {
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25VoiceWorkerHotRealtimeBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    boundedConfigValue(priorCqpskCandidates, kP25VoiceWorkerHotMaxCqpskCandidates));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                                    boundedConfigValue(priorPhase2SyncHits, kP25VoiceWorkerHotMaxPhase2SyncHits));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                                    boundedConfigValue(priorPhase2Locks, kP25VoiceWorkerHotMaxPhase2SuperframeLocks));
                            } else if (hotPhase2TrafficJob) {
                                // Block-channelize clears CQPSK/Gardner every hop.
                                // Capture 20260808_012422: after emit islands,
                                // empty eyes needed re-lock — that is coldAcquireJob
                                // / soft mask rehunt, not post-emit 240/64.
                                const bool speakerLiveHot =
                                    rx.p25SessionState.sustain.hadSuccessfulEmit ||
                                    p25Phase2SessionSpeakerSustainActive(rx) ||
                                    establishedClearStreaming;
                                // DEC-0026/0027 tried to narrow post-emit cold
                                // escalate (opp-only on capture 20260908_110146,
                                // then emptyEye-only on 20260908_112922). Capture
                                // 20260908_115603 after DEC-0027: first emit on TG
                                // 30302 was perfect (duty 0.553 emit=28), then
                                // emptyEye streak>=2 armed cold 240/64 and the
                                // *next* structure/opp/target hops burned
                                // dsp=470–605 ms (worker-busy). emptyEye itself is
                                // already ~102 ms on the hot grid. Soft mask-epoch
                                // rehunt (StructureNoTargetVoiceWindows) and
                                // coldAcquireJob cover true lost-eye / new follow.
                                // DEC-0028: never cold-escalate after the call has
                                // spoken — stay on hot search (not cold 240/64).
                                int hotBudgetMs = 80;
                                size_t hotCands = size_t{12};
                                size_t hotSyncHits = kP25VoiceWorkerHotMaxPhase2SyncHits;
                                size_t hotSfLocks = kP25VoiceWorkerHotMaxPhase2SuperframeLocks;
                                if (speakerLiveHot) {
                                    // DEC-0035 / capture 20260909_094846: live
                                    // post-emit used hot cand=8/120 while GUI
                                    // replay + CLI voicetest use cand=16/240 on
                                    // the *same* IQ and recover targetVcw (file
                                    // duty 0.43 vs live max duty 0.338 then all-A).
                                    // When the previous hop already has a target
                                    // eye, keep DEC-0019 cand=8 (060221 worker-busy).
                                    // When the target eye is gone, match replay
                                    // caps so block-channelize can re-lock Costas.
                                    //
                                    // DEC-0039 / capture 20260909_110941: after
                                    // speak, hops with companion/structure bursts
                                    // but targetVcw=0 kept cand=8 and never
                                    // matched the 095846 re-lock. Treat post-emit
                                    // no-target (and no decode) as eye-lost too.
                                    const auto& liveDiag = rx.p25VoiceDiagnostics;
                                    const bool noTargetEye =
                                        liveDiag.phase2TargetVoiceCodewords == 0 &&
                                        liveDiag.decodedFrames == 0;
                                    const bool noStructureEye =
                                        liveDiag.phase2Bursts == 0 &&
                                        liveDiag.phase2MaskedBursts == 0;
                                    const bool eyeLost =
                                        noTargetEye &&
                                        (noStructureEye ||
                                         rx.p25SessionState.sustain.hadSuccessfulEmit);
                                    if (eyeLost) {
                                        hotBudgetMs = kP25ReplayHotBudgetMs;
                                        hotCands = kP25ReplayHotCqpskCandidates;
                                        hotSyncHits = kP25ReplayHotSyncHits;
                                        hotSfLocks = kP25ReplayHotSuperframeLocks;
                                    } else {
                                        hotBudgetMs = kP25VoiceWorkerHotRealtimeBudgetMs;
                                        hotCands = kP25VoiceWorkerHotMaxCqpskCandidates;
                                        hotSyncHits = kP25VoiceWorkerHotMaxPhase2SyncHits;
                                        hotSfLocks = kP25VoiceWorkerHotMaxPhase2SuperframeLocks;
                                    }
                                }
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, hotBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    boundedConfigValue(priorCqpskCandidates, hotCands));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                                    boundedConfigValue(priorPhase2SyncHits, hotSyncHits));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                                    boundedConfigValue(priorPhase2Locks, hotSfLocks));
                            }
                            rx.p25VoiceLiveDecoder.setCqpskDiscreteFrozen(
                                p25Phase2ShouldFreezeCqpskDiscrete(rx));
                            const bool phase2HardReacquireJob =
                                rx.p25VoicePhase2 &&
                                rx.p25Phase2WideReacquireHoldWindows > 0 &&
                                !p25DiagTargetHardClear(rx.p25VoiceDiagnostics) &&
                                rx.p25VoiceDiagnostics.decodedFrames == 0 &&
                                // Hard CQPSK reset only when the Phase-2 eye is
                                // actually gone. MAC/ESS starve with live SF/mask/
                                // VCW uses invalidatePhase2StickyMaskEpoch instead.
                                rx.p25VoiceDiagnostics.phase2Bursts == 0 &&
                                rx.p25VoiceDiagnostics.phase2MaskedBursts == 0 &&
                                rx.p25VoiceDiagnostics.phase2TargetVoiceCodewords == 0;
                            if (rx.p25Phase2ForceMaskEpochRehunt) {
                                rx.p25VoiceLiveDecoder.invalidatePhase2StickyMaskEpoch();
                                rx.p25Phase2ForceMaskEpochRehunt = false;
                            }
                            if (rx.p25Phase2MaskEpochRepairHoldWindows > 0) {
                                rx.p25Phase2MaskEpochRepairHoldWindows =
                                    std::max(0, rx.p25Phase2MaskEpochRepairHoldWindows - 1);
                            }
                            if (phase2HardReacquireJob) {
                                // DEC-0014: env=1 keeps the HDQPSK stream through
                                // Costas reset. Default live stays block-channelize
                                // (105622 duty 0.685→0.095 with default-on DDC).
                                if (!p25Phase2StreamingDdcExperimentEnabled()) {
                                    rx.p25VoiceLiveDecoder.setEnableStreamingChannelDdc(false);
                                }
                                rx.p25VoiceLiveDecoder.reset();
                                if (rx.p25VoiceMaskParamsKnown) {
                                    rx.p25VoiceLiveDecoder.setPhase2MaskParameters(
                                        rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                                }
                                rx.p25VoiceLiveDecoder.setPhase2PreferredTdmaSlot(
                                    rx.p25VoiceTdmaSlotKnown,
                                    static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u));
                                rx.p25Phase2WideReacquireHoldWindows =
                                    std::max(0, rx.p25Phase2WideReacquireHoldWindows - 1);
                            } else if (rx.p25Phase2WideReacquireHoldWindows > 0) {
                                rx.p25Phase2WideReacquireHoldWindows =
                                    std::max(0, rx.p25Phase2WideReacquireHoldWindows - 1);
                            }
                            result.audio = decodeP25VoiceAudioBlock(rx, job.iq, job.sampleRateHz,
                                job.centerFreqHz, job.targetFreqHz, job.outputRateHz,
                                job.iqStartAbsolute, job.iqStartAbsoluteKnown,
                                job.contextIqSamples);
                            rx.p25DiagCqpskHypothesisChanges = std::max(
                                rx.p25DiagCqpskHypothesisChanges,
                                rx.p25VoiceLiveDecoder.cqpskDiscreteChangesBlocked());
                            if (coldAcquireJob || streamingCqpskJob || hotPhase2TrafficJob) {
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(priorDecodeBudgetMs);
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(priorCqpskCandidates);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(priorPhase2SyncHits);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(priorPhase2Locks);
                            }
                            if (rx.p25IndependentTrafficSource && rx.p25VoicePhase2 &&
                                !p25Phase2StreamingDdcExperimentEnabled()) {
                                // DEC-0014: default-on streaming DDC dropped 105622
                                // duty 0.685→0.095. Keep live GUI on block-channelize
                                // unless SDR_TOWN_P25_STREAMING_DDC=1.
                                rx.p25VoiceLiveDecoder.setEnableStreamingChannelDdc(false);
                            }
                            result.hasAudioBlock = true;
                            if (std::chrono::steady_clock::now() > decodeDeadline) {
                                // Capture 20260712_024853: the only live p2bursts=1 hit was on a
                                // job that then stamped decode-wall-timeout and was hard-dropped
                                // before publishP25VoiceDiagnostics, so acquisition never advanced.
                                // Keep useful Phase-2 evidence from over-budget jobs, but do not
                                // let an opposite-slot-only eye masquerade as selected-call progress.
                                const bool keepEvidence =
                                    result.audio.phase2TargetVoiceCodewords > 0 ||
                                    result.audio.phase2TargetMaskedBursts > 0 ||
                                    p25Phase2TargetHardClearEvidence(result.audio) ||
                                    result.audio.phase2TargetMacCrcValid ||
                                    result.audio.decodedFrames > 0 ||
                                    !result.audio.audio.empty();
                                if (keepEvidence) {
                                    result.stale = false;
                                    result.staleReason = "decode-wall-overbudget-kept";
                                } else {
                                    result.stale = true;
                                    result.staleReason = "decode-wall-timeout";
                                }
                            }
                        }
                        const auto t1 = std::chrono::steady_clock::now();
                        result.dspMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

                        staleReason.clear();
                        if (result.hasAudioBlock && !result.stale && stillCurrent(&staleReason)) {
                            result.publishVoiceDiag = true;
                            const std::string rawSpeakerGateReason = p25VoiceBlockSpeakerGateReason(result.audio);
                            const bool settleMuteBypassedForValidatedVoice =
                                job.outputMutedForSettle &&
                                p25VoiceBlockMayBypassPostArmSettle(result.audio, rawSpeakerGateReason);
                            const bool effectiveSettleMute =
                                job.outputMutedForSettle && !settleMuteBypassedForValidatedVoice;
                            result.speakerGateReason = effectiveSettleMute
                                ? std::string("post-arm-settle-muted")
                                : rawSpeakerGateReason;
                            result.speakerMayEmit =
                                result.speakerGateReason == "emit" &&
                                p25VoiceBlockHasSpeakerTimelineAudio(result.audio);
                            result.audio.phase2SpeakerGateReason = result.speakerGateReason;
                            result.speakerAudio = result.speakerMayEmit ? result.audio.audio : std::vector<float>{};
                            if (!result.speakerAudio.empty()) {
                                double sum = 0.0;
                                for (float sample : result.speakerAudio) {
                                    sum += static_cast<double>(sample) * sample;
                                }
                                result.rmsDb = 20.0 * std::log10(
                                    std::sqrt(sum / static_cast<double>(result.speakerAudio.size())) + 1e-12);
                            }
                            publishResult = true;
                        } else if (result.hasAudioBlock) {
                            if (!result.stale) {
                                result.stale = true;
                                result.staleReason = staleReason.empty() ? "stale-after-decode" : staleReason;
                            }
                            publishResult = true;
                        }
                    }
                } catch (const std::exception& ex) {
                    result.error = ex.what();
                    publishResult = true;
                } catch (...) {
                    result.error = "unknown exception";
                    publishResult = true;
                }

                if (publishResult) {
                    std::unique_lock<std::mutex> lock(p25VoiceWorkerMutex);
                    // Block until the GUI DSP worker drains completed voice
                    // results.  Never evict decoded PCM after state mutation.
                    p25VoiceWorkerCv.wait(lock, [this]() {
                        return p25VoiceWorkerStop.load(std::memory_order_acquire) ||
                               (p25VoiceCompletedResults.size() +
                                p25VoicePendingPublishDepth.load(std::memory_order_acquire)) <
                                   kP25VoiceDecodeMaxCompletedResults;
                    });
                    if (p25VoiceWorkerStop.load(std::memory_order_acquire)) {
                        continue;
                    }
                    p25VoiceCompletedResults.push_back(std::move(result));
                }
                p25VoiceWorkerBusy.store(false, std::memory_order_release);
            }
            p25VoiceWorkerBusy.store(false, std::memory_order_release);
        });
    }

void MainWindow::stopP25VoiceWorker()
{
        p25VoiceWorkerStop.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            p25VoicePendingJobs.clear();
        }
        p25VoiceWorkerCv.notify_all();
        if (p25VoiceWorkerThread.joinable()) {
            p25VoiceWorkerThread.join();
        }
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            p25VoicePendingJobs.clear();
            p25VoiceCompletedResults.clear();
        }
        p25VoiceWorkerBusy.store(false, std::memory_order_release);
    }

bool MainWindow::submitP25VoiceDecodeJob(MainWindow::P25VoiceDecodeJob job)
{
        if (!job.rx || job.iq.empty()) return false;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            if (p25VoiceWorkerStop.load(std::memory_order_acquire) || !p25VoiceWorkerThread.joinable()) {
                return false;
            }
            const size_t publishBacklog =
                p25VoiceCompletedResults.size() +
                p25VoicePendingPublishDepth.load(std::memory_order_acquire);
            const size_t runningJobs = p25VoiceWorkerBusy.load(std::memory_order_acquire) ? 1u : 0u;
            const size_t inFlightJobs = p25VoicePendingJobs.size() + publishBacklog + runningJobs;
            if (inFlightJobs >= p25VoiceDecodeMaxPendingJobsNow(job.speakerSustainDecode)) {
                // Single-flight live Phase-2: do not advance the rolling IQ cursor
                // while a prior decode is still running or waiting to publish.  A
                // queued stale window can replay/delay a different security/slot
                // state and sounds exactly like blocky doubled speech.
                p25VoiceDroppedJobs.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            job.sequence = p25VoiceJobSequence.fetch_add(1, std::memory_order_relaxed) + 1;
            p25VoicePendingJobs.push_back(std::move(job));
        }
        p25VoiceWorkerCv.notify_one();
        return true;
    }

bool MainWindow::p25VoiceWorkerCanAcceptJob()
{
        return p25VoiceWorkerCanAcceptJobForDepth(false);
    }

bool MainWindow::p25VoiceWorkerCanAcceptJobForDepth(bool speakerSustainHint)
{
        std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
        // Backpressure must happen before RollingIqWindow::takeUndecoded().
        // If any decode is running, queued, or waiting to publish, leave the
        // rolling decode cursor parked so the next scheduler pass still sees the
        // same unprocessed IQ instead of creating a stale out-of-order backlog.
        const size_t publishBacklog =
            p25VoiceCompletedResults.size() +
            p25VoicePendingPublishDepth.load(std::memory_order_acquire);
        const size_t runningJobs = p25VoiceWorkerBusy.load(std::memory_order_acquire) ? 1u : 0u;
        const size_t inFlightJobs = p25VoicePendingJobs.size() + publishBacklog + runningJobs;
        return !p25VoiceWorkerStop.load(std::memory_order_acquire) &&
               p25VoiceWorkerThread.joinable() &&
               inFlightJobs < p25VoiceDecodeMaxPendingJobsNow(speakerSustainHint) &&
               publishBacklog < kP25VoiceDecodeMaxCompletedResults;
    }

MainWindow::P25VoiceWorkerQueueSnapshot MainWindow::p25VoiceWorkerQueueSnapshot()
{
        MainWindow::P25VoiceWorkerQueueSnapshot snap;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            snap.pendingJobs = p25VoicePendingJobs.size();
            snap.pending = snap.pendingJobs > 0;
            snap.threadRunning = p25VoiceWorkerThread.joinable();
            snap.completedResults = p25VoiceCompletedResults.size();
        }
        snap.stopping = p25VoiceWorkerStop.load(std::memory_order_acquire);
        snap.busy = p25VoiceWorkerBusy.load(std::memory_order_acquire);
        snap.nextSequence = p25VoiceJobSequence.load(std::memory_order_relaxed) + 1;
        snap.droppedJobs = p25VoiceDroppedJobs.load(std::memory_order_relaxed);
        snap.droppedResults = p25VoiceDroppedResults.load(std::memory_order_relaxed);
        snap.publicationLockMisses = p25VoicePublicationLockMisses.load(std::memory_order_relaxed);
        snap.pendingPublishResults = p25VoicePendingPublishDepth.load(std::memory_order_acquire);
        return snap;
    }

std::vector<MainWindow::P25VoiceDecodeResult> MainWindow::takeP25VoiceDecodeResults()
{
        std::deque<MainWindow::P25VoiceDecodeResult> local;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            local.swap(p25VoiceCompletedResults);
            if (!local.empty()) {
                p25VoiceWorkerCv.notify_one();
            }
        }
        std::vector<MainWindow::P25VoiceDecodeResult> out;
        out.reserve(local.size());
        while (!local.empty()) {
            out.push_back(std::move(local.front()));
            local.pop_front();
        }
        return out;
    }

MainWindow::P25VoiceDecodeWorkPurge MainWindow::purgeP25VoiceDecodeWorkForSession(
        const ReceiverSessionKey& sessionKey, 
        uint64_t afterSequence)
{
        MainWindow::P25VoiceDecodeWorkPurge purged;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            for (auto it = p25VoicePendingJobs.begin(); it != p25VoicePendingJobs.end();) {
                if (it->receiverSessionKey == sessionKey &&
                    (afterSequence == 0 || it->sequence > afterSequence)) {
                    it = p25VoicePendingJobs.erase(it);
                    ++purged.pendingJobs;
                } else {
                    ++it;
                }
            }
            for (auto it = p25VoiceCompletedResults.begin(); it != p25VoiceCompletedResults.end();) {
                if (it->receiverSessionKey == sessionKey &&
                    (afterSequence == 0 || it->sequence > afterSequence)) {
                    it = p25VoiceCompletedResults.erase(it);
                    ++purged.completedResults;
                } else {
                    ++it;
                }
            }
        }
        if (purged.pendingJobs > 0) {
            p25VoiceDroppedJobs.fetch_add(purged.pendingJobs, std::memory_order_relaxed);
        }
        if (purged.completedResults > 0) {
            p25VoiceDroppedResults.fetch_add(purged.completedResults, std::memory_order_relaxed);
        }
        if (purged.total() > 0) {
            p25VoiceWorkerCv.notify_all();
        }
        return purged;
    }

P25VoicePublishOutcome MainWindow::publishP25VoiceDecodeResult(const MainWindow::P25VoiceDecodeResult& result, 
                                                       P25SpeakerPendingMap& pendingAudioByRx)
{
        if (!result.rx) return P25VoicePublishOutcome::ReceiverGone;
        // After return-to-control, the traffic Receiver may have been removed from the receivers list
        // (and its storage released). Guard against publishing stale results that would dereference
        // a now-invalid Receiver* (e.g. rx.stateMutex). This eliminates a source of freezes after
        // repeated follow/return cycles.
        {
            std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
            if (!lk.owns_lock()) {
                p25VoicePublicationLockMisses.fetch_add(1, std::memory_order_relaxed);
                return P25VoicePublishOutcome::Deferred;
            }
            bool stillActive = false;
            for (auto& r : receivers) {
                if (r && r.get() == result.rx.get()) { stillActive = true; break; }
            }
            if (!stillActive) {
                const uintptr_t rxKey = reinterpret_cast<uintptr_t>(result.rx.get());
                const uint64_t seqLog = result.sequence;
                const QString reason = QString::fromStdString(
                    result.staleReason.empty() ? std::string("receiver-object-gone") : result.staleReason);
                QTimer::singleShot(0, this, [this, rxKey, seqLog, reason]() {
                    appendP25LogLineKeyed(QString("p25-voice-worker-stale-gone:%1").arg(static_cast<qulonglong>(rxKey)),
                        QString("P25 voice worker stale/drop: seq=%1 reason=%2; receiver object is no longer active.")
                            .arg(static_cast<qulonglong>(seqLog))
                            .arg(reason),
                        1000);
                });
                return P25VoicePublishOutcome::ReceiverGone;
            }
        }
        Receiver& rx = *result.rx;
        if (!result.error.empty()) {
            const QString err = QString::fromStdString(result.error);
            QTimer::singleShot(0, this, [this, err]() {
                appendP25LogLineKeyed("p25-voice-worker-error",
                    QString("P25 voice worker decode error: %1").arg(err),
                    2500);
            });
            return P25VoicePublishOutcome::DiscardedStale;
        }
        bool stale = result.stale;
        bool publishVoiceDiag = result.publishVoiceDiag;
        std::string staleReason = result.staleReason;
        if (!result.hasAudioBlock && !stale) return P25VoicePublishOutcome::Published;
        if (!stale &&
            result.flushSeq != p25PendingAudioFlushSeq.load(std::memory_order_acquire)) {
            stale = true;
            staleReason = "audio-flush-sequence-stale";
        }
        {
            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
            if (!rxLock.owns_lock()) {
                p25VoicePublicationLockMisses.fetch_add(1, std::memory_order_relaxed);
                return P25VoicePublishOutcome::Deferred;
            }
            if (!rx.active || !rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) {
                stale = true;
                if (staleReason.empty()) staleReason = "receiver-not-active-for-voice";
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
            } else if (rx.p25VoiceResetPending) {
                stale = true;
                if (staleReason.empty()) staleReason = "voice-reset-pending";
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
            } else if (result.callSessionId != 0 &&
                       rx.p25CurrentCallSessionId != 0 &&
                       rx.p25CurrentCallSessionId != result.callSessionId) {
                stale = true;
                if (staleReason.empty()) staleReason = "call-session-changed";
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
            } else if (result.talkgroupId != 0 && rx.p25VoiceTalkgroupId != result.talkgroupId) {
                stale = true;
                if (staleReason.empty()) staleReason = "talkgroup-changed";
            } else if (result.tdmaSlotKnown &&
                       (!rx.p25VoiceTdmaSlotKnown ||
                        static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) != static_cast<uint8_t>(result.tdmaSlot & 0x01u))) {
                stale = true;
                if (staleReason.empty()) staleReason = "slot-changed";
            } else if (result.voiceFreqHz > 0.0) {
                const double liveVoiceHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
                if (liveVoiceHz > 0.0 && std::abs(liveVoiceHz - result.voiceFreqHz) > 50.0) {
                    stale = true;
                    if (staleReason.empty()) staleReason = "voice-frequency-changed";
                }
            }
            if (!stale && (result.trafficGeneration != 0 || rx.p25IndependentTrafficSource)) {
                const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                if (result.trafficGeneration == 0 ||
                    rx.p25TrafficGeneration != result.trafficGeneration ||
                    liveGen != result.trafficGeneration) {
                    stale = true;
                    if (staleReason.empty()) staleReason = "traffic-generation-stale";
                }
            }
        }
        if (stale) {
            const bool keepWallTimeoutEvidence =
                result.hasAudioBlock &&
                (result.staleReason == "decode-wall-timeout" ||
                 result.staleReason == "decode-wall-overbudget-kept") &&
                (result.audio.phase2TargetVoiceCodewords > 0 ||
                 result.audio.phase2TargetMaskedBursts > 0 ||
                 p25Phase2TargetHardClearEvidence(result.audio) ||
                 result.audio.phase2TargetMacCrcValid ||
                 result.audio.decodedFrames > 0 ||
                 !result.audio.audio.empty());
            if (!keepWallTimeoutEvidence) {
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
                const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                const uint64_t seqLog = result.sequence;
                const QString reason = QString::fromStdString(
                    staleReason.empty() ? std::string("stale") : staleReason);
                const uint32_t tgLog = result.talkgroupId;
                const uint32_t sourceLogValue = result.sourceId;
                const bool slotKnownLog = result.tdmaSlotKnown;
                const int slotLog = static_cast<int>(result.tdmaSlot & 0x01u);
                const qulonglong genLog = static_cast<qulonglong>(result.trafficGeneration);
                QTimer::singleShot(0, this, [this, rxKey, seqLog, reason, tgLog, sourceLogValue, slotKnownLog, slotLog, genLog]() {
                    appendP25LogLineKeyed(QString("p25-voice-worker-stale:%1").arg(static_cast<qulonglong>(rxKey)),
                        QString("P25 voice worker stale/drop: seq=%1 reason=%2 tg=%3 src=%4 slot=%5 generation=%6.")
                            .arg(static_cast<qulonglong>(seqLog))
                            .arg(reason)
                            .arg(tgLog)
                            .arg(sourceLogValue != 0 ? p25HexId(sourceLogValue, 6) : QStringLiteral("unknown"))
                            .arg(slotKnownLog ? QString::number(slotLog) : QStringLiteral("unknown"))
                            .arg(genLog),
                        1000);
                });
                return P25VoicePublishOutcome::DiscardedStale;
            }
            stale = false;
            publishVoiceDiag = true;
            QTimer::singleShot(0, this, [this, seq = result.sequence, tg = result.talkgroupId]() {
                appendP25LogLineKeyed(QString("p25-voice-worker-wall-kept:%1").arg(static_cast<qulonglong>(seq)),
                    QString("P25 voice worker kept over-budget decode evidence: seq=%1 tg=%2 (not hard-dropping Phase-2 bursts/VCW).")
                        .arg(static_cast<qulonglong>(seq))
                        .arg(tg),
                    1000);
            });
        }

        publishP25VoiceDiagnostics(rx, result.audio, publishVoiceDiag);

        AudioEngine* audioOutputEngine = result.speakerAudio.empty()
            ? engineForAudio.get()
            : ensureAudioOutputActive("P25 voice");
        size_t audioActiveOutputCount = 0;
        size_t audioQueuedSamples = 0;
        double audioRingFillPercent = 0.0;
        int audioUnderrunCount = 0;
        if (audioOutputEngine) {
            audioActiveOutputCount = audioOutputEngine->activeOutputCount();
            audioQueuedSamples = audioOutputEngine->getRingQueuedSamples();
            audioRingFillPercent = audioOutputEngine->getRingFillPercent();
            audioUnderrunCount = audioOutputEngine->getUnderrunCount();
        }
        writeP25Phase2AudioOutputTrace(rx, result.audio, "gui-p25-voice-worker",
            result.outputMutedForSettle, result.speakerMayEmit, audioOutputEngine != nullptr,
            audioActiveOutputCount, audioQueuedSamples, audioRingFillPercent, audioUnderrunCount,
            result.speakerAudio.size(), result.outputRateHz);

        gLastDspMicros.store(result.dspMicros, std::memory_order_relaxed);
        if (!result.speakerAudio.empty()) {
            gLastRmsDb.store(result.rmsDb, std::memory_order_relaxed);
        }
        if (result.speakerAudio.empty() && audioOutputEngine &&
            (result.audio.phase2VoiceCodewords == 0 ||
             result.audio.phase2TargetVoiceCodewords == 0 ||
             result.speakerGateReason != "emit")) {
            const qint64 emptyWindowNowMs = QDateTime::currentMSecsSinceEpoch();
            const bool preserveAudioBuffers =
                p25RecentSpeakerOutputActive(emptyWindowNowMs,
                    kP25Phase2SpeakerFollowHoldMs);
            const bool hasTrustedPhase2ClearContext =
                rx.p25VoicePhase2 &&
                !rx.p25VoiceEncrypted &&
                (p25Phase2BlockHasTrustedClearContext(result.audio) ||
                 (preserveAudioBuffers && p25Phase2AudioTailGraceActive(rx)));
            const bool hasActiveP25VoiceCall = rx.p25VoicePhase2
                ? hasTrustedPhase2ClearContext
                : (!rx.p25VoiceEncrypted && (rx.p25VoiceClearKnown || preserveAudioBuffers));
            const bool preserveLivePlayback =
                hasActiveP25VoiceCall ||
                p25Phase2ShouldPreserveLivePlaybackBuffers(rx, emptyWindowNowMs) ||
                (audioOutputEngine->getRingQueuedSamples() > 0) ||
                !p25SpeakerPendingFor(pendingAudioByRx, rx).samples.empty();
            if (!preserveLivePlayback) {
                p25Phase2ClearSpeakerPendingQueue(rx,
                    p25SpeakerPendingFor(pendingAudioByRx, rx),
                    P25PendingClearReason::UserStop);
                // Only flush when playback is truly idle. Clearing a non-empty
                // ring here races just-pushed clear PCM and the silence bridge
                // (20260720_080118 total live silence / offline WAV PASS).
                audioOutputEngine->clearBuffers();
            } else {
                const auto currentWorkerSessionActive = [&rx](const ReceiverSessionKey& key) {
                    return key.receiver == &rx &&
                        rx.p25TrafficSessionGeneration.load(std::memory_order_acquire) == key.generation;
                };
                size_t realTopUpPushed = 0;
                size_t bridgeTopUpPushed = 0;
                const size_t topUpPushed =
                    p25TopUpSpeakerPlaybackRing(audioOutputEngine,
                                                pendingAudioByRx,
                                                currentWorkerSessionActive,
                                                &realTopUpPushed,
                                                &bridgeTopUpPushed);
                if (realTopUpPushed > 0) {
                    gP25AudioLastSpeakerOutputMs.store(emptyWindowNowMs, std::memory_order_relaxed);
                    guiP25AudioLastOutputMs.store(emptyWindowNowMs, std::memory_order_relaxed);
                    guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                    guiP25AudioOutputSamples.fetch_add(static_cast<long long>(realTopUpPushed), std::memory_order_relaxed);
                    p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, emptyWindowNowMs);
                }
                if (topUpPushed > 0) {
                    const uint32_t tgLog = result.audio.talkgroupId != 0
                        ? result.audio.talkgroupId
                        : rx.p25VoiceTalkgroupId;
                    const size_t queuedAfter = audioOutputEngine->getRingQueuedSamples();
                    const double fillAfter = audioOutputEngine->getRingFillPercent();
                    const int underrunsAfter = audioOutputEngine->getUnderrunCount();
                    QTimer::singleShot(0, this, [this, tgLog, realTopUpPushed, bridgeTopUpPushed,
                                                  queuedAfter, fillAfter, underrunsAfter]() {
                        appendP25LogLineKeyed(QString("p25-audio-top-up:%1").arg(tgLog),
                            QString("P25 audio top-up: TG=%1 real=%2 bridge=%3 ringQueued=%4 ringFill=%5% underruns=%6.")
                                .arg(tgLog)
                                .arg(static_cast<qulonglong>(realTopUpPushed))
                                .arg(static_cast<qulonglong>(bridgeTopUpPushed))
                                .arg(static_cast<qulonglong>(queuedAfter))
                                .arg(fillAfter, 0, 'f', 2)
                                .arg(underrunsAfter),
                            150);
                    });
                }
            }
            // During established clear Phase-2 call, preserve pending/ring to keep audio joined across
            // decode windows that happen to contribute 0 new VCW in this slice (overlap/cadence timing).
        }

        const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
        const long long absStartLog = result.iqStartAbsoluteKnown ? static_cast<long long>(result.iqStartAbsolute) : -1LL;
        const size_t iqLog = result.iqSamples;
        const size_t freshLog = result.freshIqSamples;
        const size_t contextLog = result.contextIqSamples;
        const bool rollingLog = result.rollingDecode;
        const double cfLog = result.centerFreqHz;
        const double srLog = result.sampleRateHz;
        const double targetLog = result.audio.effectiveTargetFreqHz > 0.0
            ? result.audio.effectiveTargetFreqHz
            : result.targetFreqHz;
        const QString diagLog = p25VoiceDiagLabel(result.audio.diag);
        const QString backendLog = result.audio.backendAvailable ? QStringLiteral("yes") : QStringLiteral("no");
        const QString essLog = result.audio.phase2EssKnown
            ? (result.audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
            : QStringLiteral("unknown");
        const QString gateLog = QString::fromStdString(result.speakerGateReason);
        const QString acchLog = QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5")
            .arg(static_cast<qulonglong>(result.audio.phase2MacNominalCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacAltKindCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacBitSwapCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacSlipCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacInvertCrcValid));
        const qulonglong syncsLog = static_cast<qulonglong>(result.audio.syncs);
        const qulonglong nidsLog = static_cast<qulonglong>(result.audio.nids);
        const qulonglong decodedLog = static_cast<qulonglong>(result.audio.decodedFrames);
        const qulonglong audioSamplesLog = static_cast<qulonglong>(result.audio.audio.size());
        const qulonglong speakerSamplesLog = static_cast<qulonglong>(result.speakerAudio.size());
        const qulonglong burstsLog = static_cast<qulonglong>(result.audio.phase2Bursts);
        const qulonglong vcwLog = static_cast<qulonglong>(result.audio.phase2VoiceCodewords);
        const qulonglong targetVcwLog = static_cast<qulonglong>(result.audio.phase2TargetVoiceCodewords);
        const qulonglong oppVcwLog = static_cast<qulonglong>(result.audio.phase2OppositeVoiceCodewords);
        const qulonglong ambeAttemptsLog = static_cast<qulonglong>(result.audio.phase2AmbeDecodeAttempts);
        const qulonglong ambeAcceptedLog = static_cast<qulonglong>(result.audio.phase2AmbeAcceptedFrames);
        const qulonglong rejectedVcwLog = static_cast<qulonglong>(result.audio.phase2RejectedVoiceCodewords);
        const qulonglong wrongSlotVcwLog = static_cast<qulonglong>(result.audio.phase2WrongSlotVoiceCodewords);
        const qulonglong duplicateVcwLog = static_cast<qulonglong>(result.audio.phase2DuplicateSuppressedVoiceCodewords);
        const qulonglong absDuplicateVcwLog = static_cast<qulonglong>(result.audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
        const qulonglong seqSuppressVcwLog = static_cast<qulonglong>(result.audio.phase2SequencerSuppressedVoiceCodewords);
        const qulonglong sfLog = static_cast<qulonglong>(result.audio.phase2SuperframeBursts);
        const qulonglong maskLog = static_cast<qulonglong>(result.audio.phase2MaskedBursts);
        const qulonglong macValidLog = static_cast<qulonglong>(result.audio.phase2MacCrcValid);
        const qulonglong macTotalLog = static_cast<qulonglong>(result.audio.phase2MacPdus);
        const qulonglong droppedJobsLog = static_cast<qulonglong>(p25VoiceDroppedJobs.load(std::memory_order_relaxed));
        const qulonglong droppedResultsLog = static_cast<qulonglong>(p25VoiceDroppedResults.load(std::memory_order_relaxed));
        const long long dspMicrosLog = result.dspMicros;
        const qulonglong expVcwLogW = static_cast<qulonglong>(result.audio.phase2ExpectedVoiceCodewords);
        const qulonglong fedLogW = static_cast<qulonglong>(result.audio.phase2FedToMbelib);
        const qulonglong emitLogW = static_cast<qulonglong>(result.audio.phase2EmittedPcmFrames);
        const qulonglong gapsLogW = static_cast<qulonglong>(result.audio.phase2FeedGaps);
        const qulonglong lastAbsLogW = static_cast<qulonglong>(result.audio.phase2LastFedAbsDibit);
        const qulonglong contextVcwLogW = static_cast<qulonglong>(result.audio.phase2ContextVoiceCodewords);
        const qulonglong contextDropLogW = static_cast<qulonglong>(result.audio.phase2ContextSuppressedVoiceCodewords);
        const qulonglong pendingQueuedLogW = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesQueued);
        QTimer::singleShot(0, this, [this, rxKey, absStartLog, iqLog, freshLog, contextLog, rollingLog,
                                      cfLog, targetLog, srLog, diagLog, backendLog, gateLog, syncsLog,
                                      nidsLog, decodedLog, audioSamplesLog, speakerSamplesLog, burstsLog,
                                      vcwLog, targetVcwLog, oppVcwLog, ambeAttemptsLog, ambeAcceptedLog,
                                      rejectedVcwLog, wrongSlotVcwLog, duplicateVcwLog,
                                      absDuplicateVcwLog, seqSuppressVcwLog,
                                      sfLog, maskLog, macValidLog, macTotalLog, acchLog, essLog,
                                      droppedJobsLog, droppedResultsLog, dspMicrosLog,
                                      expVcwLogW, fedLogW, emitLogW, gapsLogW, lastAbsLogW,
                                      contextVcwLogW, contextDropLogW, pendingQueuedLogW]() {
            const QString key = QString("p25-dsp-voice-worker:%1").arg(static_cast<qulonglong>(rxKey));
            const QString line = QString("P25 DSP VOICE WORKER: rolling=%1 iq=%2 fresh=%3 context=%4 absStart=%5 sr=%6MHz cf=%7MHz target=%8MHz diag=%9 gate=%10 backend=%11 sync=%12 nid=%13 decoded=%14 audio=%15 speaker=%16 p2bursts=%17 p2vcw=%18 targetVcw=%19 oppVcw=%20 expVcw=%21 fed=%22 emitPcm=%23 gaps=%24 ctxVcw=%25 ctxDrop=%26 pendingQueued=%27 reject=%28 wrongSlot=%29 dup=%30 absDup=%31 seqDrop=%32 lastAbs=%33 p2sf=%34 p2mask=%35 p2mac=%36/%37 %38 ess=%39 dsp=%40us qDrop=%41 rDrop=%42")
                .arg(rollingLog ? "yes" : "no")
                .arg(static_cast<qulonglong>(iqLog))
                .arg(static_cast<qulonglong>(freshLog))
                .arg(static_cast<qulonglong>(contextLog))
                .arg(absStartLog)
                .arg(srLog / 1e6, 0, 'f', 3)
                .arg(cfLog / 1e6, 0, 'f', 5)
                .arg(targetLog / 1e6, 0, 'f', 5)
                .arg(diagLog)
                .arg(gateLog)
                .arg(backendLog)
                .arg(syncsLog)
                .arg(nidsLog)
                .arg(decodedLog)
                .arg(audioSamplesLog)
                .arg(speakerSamplesLog)
                .arg(burstsLog)
                .arg(vcwLog)
                .arg(targetVcwLog)
                .arg(oppVcwLog)
                .arg(expVcwLogW)
                .arg(fedLogW)
                .arg(emitLogW)
                .arg(gapsLogW)
                .arg(contextVcwLogW)
                .arg(contextDropLogW)
                .arg(pendingQueuedLogW)
                .arg(rejectedVcwLog)
                .arg(wrongSlotVcwLog)
                .arg(duplicateVcwLog)
                .arg(absDuplicateVcwLog)
                .arg(seqSuppressVcwLog)
                .arg(lastAbsLogW)
                .arg(sfLog)
                .arg(maskLog)
                .arg(macValidLog)
                .arg(macTotalLog)
                .arg(acchLog)
                .arg(essLog)
                .arg(dspMicrosLog)
                .arg(droppedJobsLog)
                .arg(droppedResultsLog);
            appendP25LogLineKeyed(key, line, 750);
        });

        if (result.speakerMayEmit && !result.speakerAudio.empty() &&
            result.audio.phase2EmittedPcmFrames > 0) {
            if (!audioOutputEngine) audioOutputEngine = ensureAudioOutputActive("decoded audio");
            if (audioOutputEngine && audioOutputEngine->activeOutputCount() > 0) {
                const double curSnrForAudio = gLastSnrDb.load(std::memory_order_relaxed);
                const bool hasCarrierForAudio = (curSnrForAudio > 2.0) ||
                    (gLastRmsDb.load(std::memory_order_relaxed) > gLastNoiseFloorDb.load(std::memory_order_relaxed) + 4.0);
                const bool carrierOk = hasCarrierForAudio ||
                    (result.audio.phase2FedToMbelib > 0 && result.audio.phase2TargetVoiceCodewords > 0) ||
                    (result.audio.phase2EmittedPcmFrames > 0 &&
                     p25VoiceBlockHasSpeakerTimelineAudio(result.audio));
                size_t pushedSamples = 0;
                const bool gateEmit =
                    result.speakerGateReason == "emit" &&
                    p25VoiceBlockMayEmitAudio(result.audio) &&
                    !result.audio.phase2StaleAudioTail;
                auto& pendingSpeaker = p25SpeakerPendingFor(pendingAudioByRx, rx);
                p25Phase2BindSpeakerPendingToCall(pendingSpeaker, rx);
                const bool hasNewPcm =
                    p25VoiceBlockHasSpeakerTimelineAudio(result.audio) &&
                    result.audio.phase2EmittedPcmFrames > 0;
                const bool phase2SpeakerSessionReady =
                    !rx.p25VoicePhase2 ||
                    (result.callSessionId != 0 &&
                     rx.p25CurrentCallSessionId != 0 &&
                     result.callSessionId == rx.p25CurrentCallSessionId &&
                     rx.p25PttGeneration != 0 &&
                     rx.p25VoiceGrantEpochMs > 0);
                if (!phase2SpeakerSessionReady) {
                    const quint32 tgLog = result.audio.talkgroupId != 0
                        ? result.audio.talkgroupId
                        : rx.p25VoiceTalkgroupId;
                    const qulonglong resultCallLog = static_cast<qulonglong>(result.callSessionId);
                    const qulonglong rxCallLog = static_cast<qulonglong>(rx.p25CurrentCallSessionId);
                    const qulonglong pttLog = static_cast<qulonglong>(rx.p25PttGeneration);
                    const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                    QTimer::singleShot(0, this, [this, tgLog, resultCallLog, rxCallLog, pttLog, grantEpochLog]() {
                        appendP25LogLineKeyed(QString("p25-audio-session-block:%1").arg(tgLog),
                            QString("P25 audio blocked: TG=%1 missing or stale Phase 2 call session resultCall=%2 rxCall=%3 pttGen=%4 grantEpoch=%5.")
                                .arg(tgLog)
                                .arg(resultCallLog)
                                .arg(rxCallLog)
                                .arg(pttLog)
                                .arg(grantEpochLog),
                            500);
                    });
                }
                std::vector<float> pushedRealAudio;
                std::vector<float> speakerAudioForQueue;
                const std::vector<float>* speakerAudioToQueue = &result.speakerAudio;
                if (rx.p25VoicePhase2 && hasNewPcm) {
                    const double outRate = audioOutputEngine
                        ? std::max(8000.0, static_cast<double>(audioOutputEngine->getSampleRate()))
                        : std::max(8000.0, result.outputRateHz);
                    const size_t phase2FrameSamples = std::max<size_t>(160,
                        static_cast<size_t>(outRate * 0.020 + 0.5));
                    speakerAudioForQueue = p25Phase2SpeakerAudioForQueue(
                        pendingSpeaker, result.audio, result.speakerAudio, phase2FrameSamples);
                    speakerAudioToQueue = &speakerAudioForQueue;
                }
                const bool hasPlayableNewPcm = !speakerAudioToQueue->empty();
                if (phase2SpeakerSessionReady && carrierOk && gateEmit &&
                    (hasPlayableNewPcm || !pendingSpeaker.samples.empty())) {
                    pushedSamples = pushP25SpeakerAudio(audioOutputEngine,
                        pendingSpeaker.samples,
                        *speakerAudioToQueue,
                        result.audioOutputIndices,
                        audioRingFillPercent,
                        !hasPlayableNewPcm,
                        &pushedRealAudio);
                }
                // Never clear accepted speaker PCM on transient gate failure.
                if (pushedSamples > 0) {
                p25Phase2ResetPlayoutBridge(rx);
                const P25P2CallAudioKey speakerKey =
                    p25CurrentPhase2AudioKey(rx, result.audio.effectiveTargetFreqHz);
                const bool bridgeAnchor =
                    p25Phase2CleanPlayoutBridgeAnchorWindow(result.audio);
                p25Phase2RememberLastEmittedSample(
                    rx, speakerKey, pushedRealAudio.empty() ? result.speakerAudio : pushedRealAudio,
                    bridgeAnchor);
                // Tap clear speaker PCM for Decode Log STT (async; never blocks DSP).
                {
                    const int sttRate = static_cast<int>(
                        std::lround(std::max(8000.0,
                            static_cast<double>(audioOutputEngine->getSampleRate()))));
                    const double sttFreq = p25TranscriptVoiceLabelHz(
                        rx, result.audio.effectiveTargetFreqHz, result.targetFreqHz);
                    const int sttSlot = result.tdmaSlotKnown
                        ? static_cast<int>(result.tdmaSlot & 0x01u)
                        : (rx.p25VoiceTdmaSlotKnown
                               ? static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u)
                               : -1);
                    const std::vector<float>& transcriptAudio =
                        pushedRealAudio.empty() ? result.speakerAudio : pushedRealAudio;
                    p25TranscriptTapSpeakerPcm(transcriptAudio.data(), transcriptAudio.size(),
                        sttRate, result.audio.talkgroupId, sttFreq, sttSlot,
                        result.audio.effectiveTargetFreqHz);
                }
                const quint32 tgLog = result.audio.talkgroupId;
                const double targetFreqLog = result.audio.effectiveTargetFreqHz > 0.0
                    ? result.audio.effectiveTargetFreqHz
                    : (result.targetFreqHz > 0.0 ? result.targetFreqHz : rx.freqHz);
                const QString slotLog = result.tdmaSlotKnown
                    ? QString::number(result.tdmaSlot & 0x01u)
                    : (rx.p25VoiceTdmaSlotKnown
                           ? QString::number(rx.p25VoiceTdmaSlot & 0x01u)
                           : QStringLiteral("unknown"));
                const qulonglong generationLog = static_cast<qulonglong>(result.trafficGeneration);
                const qulonglong seqLog = static_cast<qulonglong>(result.sequence);
                const size_t activeCount = audioOutputEngine->activeOutputCount();
                const size_t queuedAfter = audioOutputEngine->getRingQueuedSamples();
                const double fillAfter = audioOutputEngine->getRingFillPercent();
                const int underrunsAfter = audioOutputEngine->getUnderrunCount();
                const QString gateLog = QString::fromStdString(result.speakerGateReason);
                const QString essLog = result.audio.phase2EssKnown
                    ? (result.audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                    : QStringLiteral("unknown");
                const QString actionLog = QString::fromStdString(result.audio.phase2SecurityGateAction);
                const QString targetEssLog = result.audio.phase2TargetEssKnown
                    ? (result.audio.phase2TargetEssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                    : QStringLiteral("unknown");
                const QString targetSessionLog = result.audio.phase2TargetSessionAudioRelease
                    ? QStringLiteral("yes")
                    : QStringLiteral("no");
                const QString targetPttLog = result.audio.phase2TargetSecurityStateFromPtt
                    ? QStringLiteral("yes")
                    : QStringLiteral("no");
                const QString sourceLog = rx.p25VoiceSourceId != 0
                    ? p25HexId(rx.p25VoiceSourceId, 6)
                    : QStringLiteral("unknown");
                const qulonglong callSessionLog = static_cast<qulonglong>(
                    result.callSessionId != 0 ? result.callSessionId : rx.p25CurrentCallSessionId);
                const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                const qulonglong pttGenerationLog = static_cast<qulonglong>(rx.p25PttGeneration);
                const qulonglong decodedLog = static_cast<qulonglong>(result.audio.decodedFrames);
                const qulonglong targetVcwLog = static_cast<qulonglong>(result.audio.phase2TargetVoiceCodewords);
                const qulonglong oppVcwLog = static_cast<qulonglong>(result.audio.phase2OppositeVoiceCodewords);
                const qulonglong fedLog = static_cast<qulonglong>(result.audio.phase2FedToMbelib);
                const qulonglong emitLog = static_cast<qulonglong>(result.audio.phase2EmittedPcmFrames);
                const qulonglong gapLog = static_cast<qulonglong>(result.audio.phase2FeedGaps);
                const qulonglong rejectLog = static_cast<qulonglong>(result.audio.phase2RejectedVoiceCodewords);
                const qulonglong wrongSlotLog = static_cast<qulonglong>(result.audio.phase2WrongSlotVoiceCodewords);
                const qulonglong duplicateLog = static_cast<qulonglong>(result.audio.phase2DuplicateSuppressedVoiceCodewords);
                const qulonglong absDuplicateLog = static_cast<qulonglong>(result.audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
                const qulonglong seqSuppressLog = static_cast<qulonglong>(result.audio.phase2SequencerSuppressedVoiceCodewords);
                const qulonglong contextVcwLog = static_cast<qulonglong>(result.audio.phase2ContextVoiceCodewords);
                const qulonglong contextDropLog = static_cast<qulonglong>(result.audio.phase2ContextSuppressedVoiceCodewords);
                const qulonglong pendingQueuedLog = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesQueued);
                const qulonglong pendingReleaseLog = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesReleased);
                const qulonglong macValidLog = static_cast<qulonglong>(result.audio.phase2MacCrcValid);
                const qulonglong macTotalLog = static_cast<qulonglong>(result.audio.phase2MacPdus);
                const qulonglong probeAcceptedLog = static_cast<qulonglong>(result.audio.phase2DiagnosticAmbeProbeAccepted);
                const qulonglong probeAttemptsLog = static_cast<qulonglong>(result.audio.phase2DiagnosticAmbeProbeAttempts);
                guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                guiP25AudioOutputSamples.fetch_add(static_cast<long long>(pushedSamples), std::memory_order_relaxed);
                guiP25AudioDecodedFrames.fetch_add(static_cast<long long>(result.audio.decodedFrames), std::memory_order_relaxed);
                guiP25AudioAcceptedAmbeFrames.fetch_add(static_cast<long long>(result.audio.phase2AmbeAcceptedFrames), std::memory_order_relaxed);
                const qint64 speakerNowMs = QDateTime::currentMSecsSinceEpoch();
                guiP25AudioLastOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                // Always refresh speaker/sustain on a real ring push so empty-window
                // preserve + silence bridge see continuity. Follow-hold activity alone
                // stays gated (stale encrypted/wrong-slot must not extend tune hold).
                gP25AudioLastSpeakerOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                p25Phase2UpdateSessionSustainState(rx, result.audio, speakerNowMs, true);
                if (p25Phase2SpeakerOutputCanRefreshFollowActivity(result.audio)) {
                    p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, speakerNowMs);
                }
                QTimer::singleShot(0, this, [this, tgLog, targetFreqLog, slotLog, generationLog, seqLog,
                                             pushedSamples, activeCount, queuedAfter, fillAfter, underrunsAfter,
                                             gateLog, essLog, actionLog, targetEssLog, targetSessionLog, targetPttLog,
                                             sourceLog, callSessionLog, grantEpochLog, pttGenerationLog,
                                              decodedLog, targetVcwLog, oppVcwLog,
                                              fedLog, emitLog, gapLog, rejectLog, wrongSlotLog,
                                               duplicateLog, absDuplicateLog, seqSuppressLog,
                                               contextVcwLog, contextDropLog, pendingQueuedLog, pendingReleaseLog,
                                               macValidLog, macTotalLog, probeAcceptedLog, probeAttemptsLog]() {
                    appendP25LogLineKeyed(QString("p25-audio-output:%1").arg(tgLog),
                        QString("P25 audio output: TG=%1 target=%2MHz slot=%3 gen=%4 seq=%5 pushed=%6 samples gate=%7 decoded=%8 targetVcw=%9 oppVcw=%10 fed=%11 emitPcm=%12 gaps=%13 ctxVcw=%14 ctxDrop=%15 reject=%16 wrongSlot=%17 dup=%18 absDup=%19 seqDrop=%20 pendingQueued=%21 pendingRel=%22 p2mac=%23/%24 probe=%25/%26 ess=%27 targetEss=%28 targetSession=%29 targetPtt=%30 action=%31 activeOutputs=%32 ringQueued=%33 ringFill=%34% underruns=%35 src=%36 call=%37 grantEpoch=%38 pttGen=%39.")
                            .arg(tgLog)
                            .arg(targetFreqLog / 1e6, 0, 'f', 5)
                            .arg(slotLog)
                            .arg(generationLog)
                            .arg(seqLog)
                            .arg(static_cast<qulonglong>(pushedSamples))
                            .arg(gateLog)
                            .arg(decodedLog)
                            .arg(targetVcwLog)
                            .arg(oppVcwLog)
                            .arg(fedLog)
                            .arg(emitLog)
                            .arg(gapLog)
                            .arg(contextVcwLog)
                            .arg(contextDropLog)
                            .arg(rejectLog)
                            .arg(wrongSlotLog)
                            .arg(duplicateLog)
                            .arg(absDuplicateLog)
                            .arg(seqSuppressLog)
                            .arg(pendingQueuedLog)
                            .arg(pendingReleaseLog)
                            .arg(macValidLog)
                            .arg(macTotalLog)
                            .arg(probeAcceptedLog)
                            .arg(probeAttemptsLog)
                            .arg(essLog)
                            .arg(targetEssLog)
                            .arg(targetSessionLog)
                            .arg(targetPttLog)
                            .arg(actionLog)
                            .arg(static_cast<qulonglong>(activeCount))
                            .arg(static_cast<qulonglong>(queuedAfter))
                            .arg(fillAfter, 0, 'f', 2)
                            .arg(underrunsAfter)
                            .arg(sourceLog)
                            .arg(callSessionLog)
                            .arg(grantEpochLog)
                            .arg(pttGenerationLog),
                        150);
                });
                }
            } else {
                const quint32 tgLog = result.audio.talkgroupId;
                const size_t samplesLog = result.speakerAudio.size();
                QTimer::singleShot(0, this, [this, tgLog, samplesLog]() {
                    appendP25LogLineKeyed(QString("p25-audio-output-missing:%1").arg(tgLog),
                        QString("P25 audio output blocked: TG=%1 decoded=%2 samples but no active playback output.")
                            .arg(tgLog)
                            .arg(static_cast<qulonglong>(samplesLog)),
                        1000);
                });
            }
        } else if (result.hasAudioBlock &&
                   result.speakerGateReason != "emit" &&
                   rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Encrypted &&
                   !p25Phase2SessionSpeakerSustainActive(rx)) {
            p25Phase2ClearSpeakerPlaybackQueue(rx,
                p25SpeakerPendingFor(pendingAudioByRx, rx),
                P25PendingClearReason::EncryptedState);
        }
        return P25VoicePublishOutcome::Published;
    }
