#include "P25DecodeConfig.h"

#include "P25AppGlobals.h"
#include "P25TalkgroupRegistry.h"
#include "P25VoiceSession.h"
#include "P25VoiceTiming.h"
#include "Receiver.h"

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

P25LiveDecoderConfig p25DiagnosticDecoderConfig()
{
    P25LiveDecoderConfig cfg;
    // GUI/CLI diagnostics feed processIq() with independent or overlapping IQ
    // windows. A stateful DDC is only valid for strictly contiguous RF chunks.
    cfg.enableStreamingChannelDdc = false;
    cfg.enableC4fmFixedPhaseSearch = true;
    cfg.maxC4fmFixedPhaseCandidates = 10;
    cfg.maxFrameSyncs = 12;
    cfg.maxRawTsbkBlocksPerFrame = 8;
    cfg.enablePhase2Decode = true;
    return cfg;
}

P25LiveDecoderConfig p25RealtimeControlDecoderConfig()
{
    P25LiveDecoderConfig cfg = p25DiagnosticDecoderConfig();
    cfg.maxRawTsbkBlocksPerFrame = 8;
    cfg.realtimeVoiceSearch = false;
    cfg.stopC4fmSearchOnHardLock = true;
    cfg.stopCqpskSearchOnHardLock = false;
    cfg.maxCqpskSearchCandidates = 32;
    // A P25 trunking control channel is decoded for TSBK/MBT grant data here.
    // Phase 2 traffic burst/ESS search belongs on the granted voice receiver;
    // running it on the CC produces noisy "ESS unknown" telemetry and can make
    // GUI follow diagnostics diverge from the CLI waitgrant path.
    cfg.enablePhase2Decode = false;
    return cfg;
}

P25LiveDecoderConfig p25CliControlGrantDecoderConfig()
{
    P25LiveDecoderConfig cfg = p25RealtimeControlDecoderConfig();
    // P25 trunking control channels carry followable grants as Phase 1 TSBK/MBT
    // control messages even when the granted traffic channel is Phase 2 TDMA.
    // The control carrier can be C4FM or CQPSK/LSM, so keep the same full
    // C4FM/CQPSK acquisition family used by the GUI control worker.  Only the
    // traffic-channel Phase 2 burst/ESS search remains disabled here; the full
    // Phase 2 chain is enabled again by p25VoiceDecoderConfig(true) after a
    // grant is selected and the receiver is retuned to the traffic channel.
    cfg.enablePhase2Decode = false;
    cfg.realtimeVoiceSearch = false;
    cfg.stopC4fmSearchOnHardLock = true;
    cfg.enableCqpskSearch = true;
    cfg.stopCqpskSearchOnHardLock = false;
    cfg.maxFrameSyncs = 5;
    cfg.maxRawTsbkBlocksPerFrame = 8;
    cfg.maxC4fmFixedPhaseCandidates = 10;
    cfg.maxCqpskSearchCandidates = 32;
    return cfg;
}

P25LiveDecoderConfig p25VoiceDecoderConfig(bool phase2,
                                                  P25VoiceDecodeProfile profile)
{
    P25LiveDecoderConfig cfg = profile == P25VoiceDecodeProfile::Forensic
        ? p25DiagnosticDecoderConfig()
        : p25RealtimeVoiceDecoderConfig();
    // Default off here. Dedicated traffic sources enable streaming DDC in
    // p25VoiceDecoderConfigForReceiver() (DEC-0014). Forensic/CC/overlapping
    // diagnostic windows stay stateless block-channelize.
    cfg.enableStreamingChannelDdc = false;
    if (phase2 && profile == P25VoiceDecodeProfile::Realtime &&
        p25Phase2StreamingDdcExperimentEnabled()) {
        cfg.enableStreamingChannelDdc = true;
    }
    // Phase 1 C4FM/control-channel symbols are 4800 sps.
    // Phase 2 H-DQPSK air rate is 6000 sps (TIA-102 / SDRTrunk P25P2DecoderHDQPSK
    // `super(6000.0)`).  The old "same air symbol rate" 4800 override was a
    // regression: Gardner strobes at 4800 never line up with 180-symbol /
    // 30 ms TDMA slots, so clear RF windows report p2bursts=0 offline+live.
    // CQPSK vs C4FM acquisition is selected via phase2CqpskTrafficDemod.
    cfg.symbolRate = phase2 ? 6000.0 : 4800.0;
    cfg.channelBandwidthHz = 12500.0;
    // Phase-2 DDC/channelizer uses SDRTrunk HDQPSK pass 6500 / stop 7200
    // (p25ChannelizerLowpass). Phase-1 keeps 0.58*BW. Independent traffic
    // enables streaming DDC in p25VoiceDecoderConfigForReceiver (DEC-0014).
    // SDRTrunk HDQPSK defaults ~25 kHz (~4.17 SPS).  Keep >=8 SPS locally so
    // Gardner/TED has headroom after channelize clamps to symbolRate*8..10.
    cfg.workSampleRate = phase2 ? 48000.0 : 48000.0;
    cfg.phase2CqpskTrafficDemod = phase2;
    cfg.maxFrameSyncBitErrors = phase2 ? std::max(3, static_cast<int>(cfg.maxFrameSyncBitErrors))
                                       : cfg.maxFrameSyncBitErrors;
    if (phase2) {
        cfg.stopCqpskSearchOnHardLock = true;
        cfg.realtimeVoiceSearch = profile == P25VoiceDecodeProfile::Realtime;
        // Match SDRTrunk CostasLoop BW_300: bandwidth = 2π/300 ≈ 0.02094.
        cfg.cqpskCarrierLoopBandwidth = (2.0 * 3.14159265358979323846) / 300.0;
        cfg.cqpskCarrierLoopMaxCorrectionHz = 3000.0; // symbolRate/2, Costas max
        if (profile == P25VoiceDecodeProfile::Realtime) {
            // Live Phase-2 traffic must stay bounded.  Keep a cheap C4FM
            // Gardner fallback for odd captures, but do not run the offline
            // fixed-phase C4FM grid on every rolling voice window.  The real
            // Phase-2 traffic path is CQPSK/H-DQPSK and the decoder maintains a
            // sticky CQPSK lock after hard MAC/ESS evidence appears.
            //
            // 2026-07-10 field capture (TG30304 clear @ 420.725): forensic
            // CQPSK search found Phase-2 bursts/MAC while the previous realtime
            // budget (85ms / 20 candidates / 48 sync hits) stayed at p2bursts=0
            // for the entire follow.  Raise the acquisition ceiling enough to
            // reach the same eye without restoring the unbounded forensic grid.
            cfg.enableC4fmFixedPhaseSearch = false;
            cfg.maxC4fmFixedPhaseCandidates = 0;
            cfg.maxFrameSyncs = std::min<size_t>(cfg.maxFrameSyncs, 6);
            cfg.maxRawTsbkBlocksPerFrame = std::min<size_t>(cfg.maxRawTsbkBlocksPerFrame, 4);
            cfg.enablePhase1Decode = false;
            // Keep the first CQPSK pass cheap so cold one-RTL can try several
            // fresh IQ windows inside a short PTT instead of one 160–450 ms pass.
            // Once CQPSK locks, keep budget modest so the worker can submit
            // many short continuous windows (SDRTrunk stream rate) instead of
            // one heavy 300 ms job that leaves 1–2 s of RF undecodeed.
            cfg.realtimeDecodeBudgetMs = 220;
            cfg.maxPhase2SyncHits = 72;
            cfg.maxPhase2SuperframeLocks = 4;
        } else {
            // Phase-2 traffic is H-DQPSK/CQPSK-family.  The fixed C4FM grid is
            // useful for Phase-1 diagnostics but can create strong-looking
            // Phase-2 false positives at +/- tone offsets, which validates
            // clipped/scrambled audio in replay while the live path correctly
            // refuses it.  Forensic Phase-2 replay should spend its budget on
            // the CQPSK candidate family we use for real traffic follow.
            cfg.enableC4fmFixedPhaseSearch = false;
            cfg.maxC4fmFixedPhaseCandidates = 0;
            cfg.stopCqpskSearchOnHardLock = false;
            cfg.maxCqpskSearchCandidates = 0;
        }
        // Phase 2 traffic follow must remain real-time.  Leaving CQPSK search
        // unbounded can evaluate over a thousand candidates per rolling voice
        // window, which field testing showed as GUI stalls and missed follow
        // audio.  Keep acquisition broad enough for LSM/H-DQPSK, then rely on
        // the persistent CQPSK lock once hard MAC/ESS evidence appears.
        // Keep cold acquire cheap enough that annotate/commit still runs inside
        // the worker wall.  28–32 candidates @ ~560ms total was still cutting
        // off mask/audio candidates on slower diagnostic runs, so keep a small
        // margin below the 700 ms cold wall.
        // extraction on live one-RTL follows (080701).
        if (profile == P25VoiceDecodeProfile::Realtime) cfg.maxCqpskSearchCandidates = 96;
        cfg.cqpskLockMissTolerance = 32;
        // SDRTrunk P25P2DecoderHDQPSK: LPF (~6500/7200) + AGC only — no RRC.
        // RRC α=0.35 was a LSM/P1-style matched filter that mistimed the P2 eye.
        cfg.cqpskUseMatchedRrcFilter = false;
        cfg.cqpskRrcAlpha = 0.20;
    }
    return cfg;
}

P25LiveDecoderConfig p25VoiceDecoderConfigForReceiver(const Receiver& rx,
                                                      P25VoiceDecodeProfile profile)
{
    P25LiveDecoderConfig cfg = p25VoiceDecoderConfig(rx.p25VoicePhase2, profile);
    if (!rx.p25VoicePhase2) return cfg;
    // SDRTrunk HDQPSK.receive is this streaming DDC. DEC-0014 measured
    // default-on for every independent traffic source: 105622 TG 30003
    // slot 0 skip=97334 duty 0.685→0.095 (emptyWindows=80/89). Keep it
    // opt-in. Do not turn it on for CC or forensic overlapping windows.
    // SDR_TOWN_P25_STREAMING_DDC=1 enables it; locked hops are 80 ms.
    if (profile == P25VoiceDecodeProfile::Realtime &&
        rx.p25IndependentTrafficSource &&
        p25Phase2StreamingDdcExperimentEnabled()) {
        cfg.enableStreamingChannelDdc = true;
    }
    // SDRTrunk queues Phase-2 voice until PTT/ESS establishes encryption state;
    // it does not let AMBE plausibility choose a sticky XOR mask phase. Field
    // replay at skip=346000 showed zero-score AMBE-only phases poisoning MAC/ESS
    // acquisition before the real phase=0 CRC-valid window arrived.
    cfg.allowPhase2SoftAmbeMaskPhaseLock = false;
    cfg.phase2PreferredTdmaSlotKnown = rx.p25VoiceTdmaSlotKnown;
    cfg.phase2PreferredTdmaSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);

    const double trafficHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
    const double controlHz = rx.p25TrafficControlFreqHz > 0.0 ? rx.p25TrafficControlFreqHz : 0.0;
    if (rx.p25IndependentTrafficSource && profile == P25VoiceDecodeProfile::Realtime) {
        // Capture 20260712_021852: broader 56/170ms cold windows still left
        // p2bursts≈0 while worker jobs occasionally ran multi-second and hit
        // decode-wall-timeout. Prefer more frequent ~95–110ms eyes with hard-lock
        // stop (avoids ACCH hang mid-candidate) and spread offset recovery across
        // jobs instead of one heavy in-job probe.
        cfg.maxCqpskSearchCandidates = std::max(cfg.maxCqpskSearchCandidates, size_t{96});
        cfg.realtimeDecodeBudgetMs = std::max(cfg.realtimeDecodeBudgetMs, 220);
        cfg.maxPhase2SyncHits = std::max(cfg.maxPhase2SyncHits, size_t{72});
        cfg.maxPhase2SuperframeLocks = std::max(cfg.maxPhase2SuperframeLocks, size_t{4});
        cfg.stopCqpskSearchOnHardLock = true;
    }
    if (rx.p25TrafficRetunesPrimary && controlHz > 0.0 && trafficHz > 0.0) {
        const double ccBleedHz = std::abs(controlHz - trafficHz);
        // Wideband IQ that is still centred on the control channel can leak a
        // strong CC into a soft traffic channelizer.  One-RTL physical retune
        // (DEC-0015) parks the granted 12.5 kHz channel just right of DC
        // (~11 kHz), so a CC that is hundreds of kHz away is rejected by the
        // 12.5 kHz channelizer.  The old 6.5 kHz clamp on that path starved
        // Phase-2 CQPSK/RRC (2026-07-10 capture: live/realtime p2bursts=0
        // while forensic 12.5 kHz found MAC).
        const bool physicallyOnVoice =
            rx.p25IndependentTrafficSource &&
            std::isfinite(trafficHz) && trafficHz > 0.0;
        if (!physicallyOnVoice && ccBleedHz >= 150e3 && ccBleedHz <= 400e3) {
            cfg.channelBandwidthHz = 10000.0;
            if (profile == P25VoiceDecodeProfile::Realtime) {
                cfg.maxCqpskSearchCandidates = std::max(cfg.maxCqpskSearchCandidates, size_t{56});
            }
        } else if (physicallyOnVoice) {
            cfg.channelBandwidthHz = 12500.0;
        }
    }
    return cfg;
}

int p25CliDecodeScore(const P25LiveDecodeResult& result)
{
    int score = 0;
    for (const auto& block : result.rawTsbkBlocks) {
        if (block.fecDecoded && block.crcValid) score += 400;
        else if (block.fecDecoded) score += 4;
    }
    for (const auto& pdu : result.phase1Pdus) {
        if (pdu.headerFecDecoded && pdu.headerCrcValid) score += pdu.format == 23 ? 420 : 300;
        else if (pdu.headerFecDecoded) score += 4;
    }
    for (const auto& nid : result.nids) {
        if (nid.fecValidated) score += 80;
    }
    score += static_cast<int>(std::min<size_t>(result.syncs.size(), 8)) * 4;
    score += static_cast<int>(result.stats.phase2MacCrcValid) * 400;
    if (result.stats.phase2EssKnown) score += 300;
    if (result.stats.phase2MaskPhaseKnown) score += 200;
    const bool phase2MetadataTrusted = result.stats.phase2MacCrcValid > 0 || result.stats.phase2EssKnown;
    if (phase2MetadataTrusted) {
        score += static_cast<int>(std::min<size_t>(result.stats.phase2SuperframeBursts, 12)) * 10;
        score += static_cast<int>(std::min<size_t>(result.stats.phase2VoiceCodewords, 24));
    } else {
        score += static_cast<int>(std::min<size_t>(result.stats.phase2VoiceCodewords, 6));
    }
    if (result.stats.bestFrameSyncBitErrors >= 0) score += std::max(0, 12 - result.stats.bestFrameSyncBitErrors);
    if (result.stats.bestNidBchDistance >= 0) score += std::max(0, 16 - result.stats.bestNidBchDistance);
    return score;
}

bool p25ControlDecodeHasTrustedPayload(const P25LiveDecodeResult& result)
{
    const bool trustedTsbk = std::any_of(result.rawTsbkBlocks.begin(), result.rawTsbkBlocks.end(),
        [](const P25TsbkBlock& block) {
            return block.fecDecoded && block.crcValid;
        });
    if (trustedTsbk) return true;

    const bool trustedPdu = std::any_of(result.phase1Pdus.begin(), result.phase1Pdus.end(),
        [](const P25Phase1PduMessage& pdu) {
            return pdu.headerFecDecoded && pdu.headerCrcValid;
        });
    return trustedPdu || result.stats.phase2MacCrcValid > 0;
}

bool p25ControlDecodeHasValidatedNid(const P25LiveDecodeResult& result)
{
    if (result.stats.bestNidValid) return true;
    return std::any_of(result.nids.begin(), result.nids.end(), [](const P25Nid& nid) {
        return nid.fecValidated;
    });
}

bool p25ControlDecodeShouldProbeOffsets(const P25LiveDecodeResult& result)
{
    if (p25ControlDecodeHasTrustedPayload(result)) return false;
    if (p25ControlDecodeHasValidatedNid(result) && result.stats.bestNidBchDistance >= 0 &&
        result.stats.bestNidBchDistance <= 8) {
        return false;
    }
    return result.stats.bestFrameSyncBitErrors < 0 ||
        result.stats.bestFrameSyncBitErrors <= 5 ||
        result.stats.bestNidBchDistance >= 0 ||
        !result.syncs.empty();
}

P25LiveDecodeResult decodeP25ControlWithOffsetProbe(P25LiveDecoder& decoder,
                                                           const std::vector<std::complex<float>>& iq,
                                                           double sampleRateHz,
                                                           double centerFreqHz,
                                                           double nominalTargetHz,
                                                           double* effectiveTargetHz)
{
    static std::mutex offsetMutex;
    static double lockedNominalTargetHz = 0.0;
    static double lockedOffsetHz = 0.0;
    static int lockedTrust = 0;
    static qint64 lastProbeMs = 0;

    auto resetLockIfNeeded = [&]() {
        if (!std::isfinite(nominalTargetHz) || nominalTargetHz <= 0.0 ||
            lockedNominalTargetHz <= 0.0 ||
            std::abs(lockedNominalTargetHz - nominalTargetHz) > 50.0) {
            lockedNominalTargetHz = nominalTargetHz;
            lockedOffsetHz = 0.0;
            lockedTrust = 0;
            lastProbeMs = 0;
        }
    };

    double preferredOffsetHz = 0.0;
    int preferredTrust = 0;
    {
        std::lock_guard<std::mutex> lock(offsetMutex);
        resetLockIfNeeded();
        preferredOffsetHz = lockedOffsetHz;
        preferredTrust = lockedTrust;
    }

    P25LiveDecoder baseline = decoder;
    const double firstTargetHz = nominalTargetHz + ((preferredTrust > 0 && std::abs(preferredOffsetHz) <= 18000.0)
        ? preferredOffsetHz
        : 0.0);
    auto best = decoder.processIq(iq, sampleRateHz, centerFreqHz, firstTargetHz);
    P25LiveDecoder bestDecoder = decoder;
    double bestTargetHz = firstTargetHz;
    int bestScore = p25CliDecodeScore(best);
    const bool firstHardLock = p25ControlDecodeHasTrustedPayload(best);

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool shouldProbe = !firstHardLock && p25ControlDecodeShouldProbeOffsets(best);
    {
        std::lock_guard<std::mutex> lock(offsetMutex);
        if (preferredTrust <= 0 && lastProbeMs > 0 && nowMs - lastProbeMs < 8000) {
            shouldProbe = false;
        } else if (preferredTrust > 0 && lastProbeMs > 0 && nowMs - lastProbeMs < 10000 &&
                   p25ControlDecodeHasValidatedNid(best)) {
            shouldProbe = false;
        }
        if (shouldProbe) lastProbeMs = nowMs;
    }

    if (shouldProbe) {
        std::vector<double> offsets;
        auto addOffset = [&](double hz) {
            if (!std::isfinite(hz) || std::abs(hz) > 6500.0) return;
            if (std::any_of(offsets.begin(), offsets.end(), [&](double existing) {
                    return std::abs(existing - hz) < 50.0;
                })) {
                return;
            }
            offsets.push_back(hz);
        };
        addOffset(0.0);
        if (preferredTrust > 0) addOffset(preferredOffsetHz);
        for (double hz : {1250.0, -1250.0}) {
            addOffset(hz);
        }
        const bool baselineHasNoSync =
            best.stats.bestFrameSyncBitErrors < 0 &&
            best.stats.bestNidBchDistance < 0 &&
            best.syncs.empty();
        if (baselineHasNoSync) {
            addOffset(2500.0);
            addOffset(-2500.0);
        }

        for (double offsetHz : offsets) {
            const double candidateTargetHz = nominalTargetHz + offsetHz;
            if (std::abs(candidateTargetHz - firstTargetHz) < 50.0) continue;
            if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0 ||
                std::abs(candidateTargetHz - centerFreqHz) > sampleRateHz * 0.48) {
                continue;
            }

            P25LiveDecoder candidateDecoder = baseline;
            auto candidate = candidateDecoder.processIq(iq, sampleRateHz, centerFreqHz, candidateTargetHz);
            const int candidateScore = p25CliDecodeScore(candidate);
            const bool candidateHard = p25ControlDecodeHasTrustedPayload(candidate);
            const bool bestHard = p25ControlDecodeHasTrustedPayload(best);
            const bool candidateNid = p25ControlDecodeHasValidatedNid(candidate);
            const bool bestNid = p25ControlDecodeHasValidatedNid(best);
            const bool betterNidDistance =
                candidate.stats.bestNidBchDistance >= 0 &&
                (best.stats.bestNidBchDistance < 0 ||
                 candidate.stats.bestNidBchDistance + 3 < best.stats.bestNidBchDistance);

            const bool candidateLargeOffset = std::abs(offsetHz) > 5000.0;
            const bool candidateStrongEnough =
                !candidateLargeOffset ||
                (candidateHard && candidateScore > bestScore + 300);

            if (candidateStrongEnough &&
                ((candidateHard && !bestHard) ||
                (candidateHard == bestHard && candidateNid && !bestNid) ||
                (candidateHard == bestHard && candidateNid == bestNid && betterNidDistance) ||
                candidateScore > bestScore + 24)) {
                bestScore = candidateScore;
                best = std::move(candidate);
                bestDecoder = std::move(candidateDecoder);
                bestTargetHz = candidateTargetHz;
                if (candidateHard) break;
            }
        }
    }

    const double selectedOffsetHz = bestTargetHz - nominalTargetHz;
    {
        std::lock_guard<std::mutex> lock(offsetMutex);
        resetLockIfNeeded();
        if (p25ControlDecodeHasTrustedPayload(best) || p25ControlDecodeHasValidatedNid(best)) {
            if (std::abs(selectedOffsetHz) >= 50.0) {
                lockedOffsetHz = selectedOffsetHz;
                lockedTrust = std::min(lockedTrust + (p25ControlDecodeHasTrustedPayload(best) ? 3 : 1), 20);
            } else {
                lockedOffsetHz *= 0.65;
                if (std::abs(lockedOffsetHz) < 50.0) lockedOffsetHz = 0.0;
                lockedTrust = std::max(0, lockedTrust - 1);
            }
        } else if (lockedTrust > 0) {
            --lockedTrust;
            if (lockedTrust == 0) lockedOffsetHz = 0.0;
        }
    }

    if (std::abs(selectedOffsetHz) >= 50.0) {
        std::ostringstream msg;
        msg << "P25 control target-offset probe selected "
            << std::fixed << std::setprecision(0) << selectedOffsetHz
            << " Hz (effective target " << std::setprecision(5) << (bestTargetHz / 1e6)
            << " MHz, score " << bestScore << ")";
        best.warnings.push_back(msg.str());
    }

    decoder = std::move(bestDecoder);
    if (effectiveTargetHz) *effectiveTargetHz = bestTargetHz;
    return best;
}

void p25SeedAnalyzerNacFromDecode(P25ControlChannelAnalyzer& analyzer,
                                         const P25LiveDecodeResult& result)
{
    for (const auto& nid : result.nids) {
        if (nid.fecValidated) {
            analyzer.setNac(nid.nac);
            return;
        }
    }
}

void printP25CliDecodeReport(const std::string& label,
                                    int devIndex,
                                    double centerFreqHz,
                                    double sampleRateHz,
                                    double targetHz,
                                    const P25LiveDecodeResult& result,
                                    P25ControlChannelAnalyzer& analyzer)
{
    p25SeedAnalyzerNacFromDecode(analyzer, result);
    const bool nidLock = p25DecodeResultHasNidLock(result);
    size_t trustedTsbk = 0;
    size_t trustedPhase1Pdu = 0;
    size_t trustedPhase2Mac = 0;
    size_t controlVoiceGrantEvents = 0;
    size_t controlResolvedVoiceGrantEvents = 0;
    size_t controlUnresolvedVoiceGrantEvents = 0;
    std::map<std::string, size_t> trustedControlOps;
    std::vector<P25PendingVoiceGrant> pendingVoiceGrants;
    for (const auto& block : result.rawTsbkBlocks) {
        if (block.fecDecoded && block.crcValid) {
            ++trustedTsbk;
            ++trustedControlOps[p25ControlAuditTsbkKey(block.bytes)];
        }
    }
    for (const auto& pdu : result.phase2MacPdus) {
        if (pdu.fecDecoded && pdu.crcValid) {
            ++trustedPhase2Mac;
            ++trustedControlOps[p25ControlAuditPhase2MacKey(pdu)];
        }
    }
    for (const auto& pdu : result.phase1Pdus) {
        if (pdu.headerFecDecoded && pdu.headerCrcValid) {
            ++trustedPhase1Pdu;
            ++trustedControlOps[p25ControlAuditPhase1PduKey(pdu)];
        }
    }
    const QString lockStage = p25LiveLockStageText(result, trustedTsbk);

    std::cout << label;
    if (devIndex >= 0) std::cout << " dev=" << devIndex;
    std::cout << " target=" << (targetHz / 1e6) << " MHz"
              << " center=" << (centerFreqHz / 1e6) << " MHz"
              << " sr=" << (sampleRateHz / 1e6) << " MHz"
              << " stage=" << lockStage.toStdString()
              << " path=" << (result.stats.demodPath.empty() ? "unknown" : result.stats.demodPath)
              << " cqpskLock=" << (result.stats.cqpskLockActive ? "active" : "new")
              << "/" << (result.stats.cqpskLockUsed ? "used" : "search")
              << "/" << (result.stats.cqpskLockUpdated ? "updated" : "held")
              << " cqpskPhase=" << result.stats.cqpskSymbolPhaseFraction
              << " cqpskFine=" << (result.stats.cqpskFineCorrectionApplied ? result.stats.cqpskFineRotationRad : 0.0)
              << " cqpskResidualHz=" << result.stats.cqpskResidualCarrierHz
              << " cqpskErrRms=" << result.stats.cqpskPhaseErrorRmsRad
              << " cqpskTrust=" << result.stats.cqpskLockTrustScore
              << " cqpskMiss=" << result.stats.cqpskLockMisses
              << " cqpskSticky=" << (result.stats.cqpskStickyOverride ? "yes" : "no")
              << " targetOffsetHz=" << result.stats.inputTargetOffsetHz
              << " chanSr=" << result.stats.channelSampleRate
              << " discMeanHz=" << result.stats.discriminatorMeanHz
              << " iq=" << result.stats.inputSamples
              << " symbols=" << result.stats.symbols
              << " softQ=" << result.stats.softDecisionQuality
              << " softLlr=" << result.stats.softBitLlrMean
              << " softLow=" << result.stats.softLowConfidenceSymbols << "/" << result.stats.softDecisionSymbols
              << " syncs=" << result.syncs.size()
              << " bestSyncErr=" << result.stats.bestFrameSyncBitErrors
              << " bestBit=" << result.stats.bestFrameSyncBitOffset
              << " bestAligned=" << (result.stats.bestFrameSyncBitAligned ? "yes" : "no")
              << " bestInv=" << (result.stats.bestFrameSyncInverted ? "yes" : "no")
              << " nidLock=" << (nidLock ? "yes" : "no")
              << " p2bursts=" << result.stats.phase2Bursts
              << " p2vcw=" << result.stats.phase2VoiceCodewords
              << " p2sf=" << result.stats.phase2SuperframeBursts
              << " p2mask=" << result.stats.phase2MaskedBursts
              << " p2phase=" << (result.stats.phase2MaskPhaseKnown ? std::to_string(result.stats.phase2MaskPhase) : std::string("-"))
              << "/" << result.stats.phase2MaskPhaseMacCrcValid
              << " p2phaseScore=" << result.stats.phase2MaskPhaseScore
              << " p1pdu=" << result.stats.phase1PduCrcValid << "/" << result.stats.phase1PduHeaders
              << " p1ambtc=" << result.stats.phase1AmbtcPdus
               << " p2mac=" << result.stats.phase2MacCrcValid << "/" << result.stats.phase2MacPdus
               << " " << p25Phase2AcchStatsText(result.stats).toStdString()
               << " p2ess=" << (result.stats.phase2EssKnown ? (result.stats.phase2EssEncrypted ? "enc" : "clear") : "unknown")
               << " p2isch=" << result.stats.phase2IschDecoded << "/" << result.stats.phase2IschSync
               << " p2syncAdj=" << result.stats.phase2SyncOffsetCorrections
               << "/" << result.stats.phase2SyncOffsetCorrectionDibits;
    if (result.stats.bestPhase2SyncErrors >= 0) {
        std::cout << " p2bestErr=" << result.stats.bestPhase2SyncErrors
                  << " p2bestDibit=" << result.stats.bestPhase2SyncDibitOffset;
    }
    if (result.stats.bestNidBchDistance >= 0) {
        std::cout << " bestNidDist=" << result.stats.bestNidBchDistance
                  << " bestNAC=0x" << std::hex << result.stats.bestNidNac << std::dec
                  << " bestDUID=0x" << std::hex << static_cast<int>(result.stats.bestNidRawDuid) << std::dec
                  << " bestNid=" << (result.stats.bestNidValid ? "valid" : "fail");
    }
    std::cout << " voiceBackend=" << (result.stats.voiceBackendAvailable ? "yes" : "no") << "\n";

    for (const auto& nid : result.nids) {
        std::cout << "  NID bit=" << nid.bitOffset
                  << " NAC=0x" << std::hex << nid.nac << std::dec
                  << " DUID=" << P25LiveDecoder::dataUnitIdToString(nid.duid)
                  << " fec=" << (nid.fecValidated ? "validated" : "fail")
                  << " corrected=" << nid.correctedBitErrors << "\n";
    }

    if (!result.rawTsbkBlocks.empty()) {
        std::cout << "  raw TSDU block candidates=" << result.rawTsbkBlocks.size()
                  << " trusted=" << trustedTsbk
                  << " (trusted means trellis-decoded and CRC-valid)\n";
        if (trustedTsbk == 0) {
            size_t shown = 0;
            for (const auto& block : result.rawTsbkBlocks) {
                if (++shown > 6) break;
                std::cout << "  candidate TSBK bit=" << block.bitOffset
                          << " fec=" << (block.fecDecoded ? "decoded" : "fail")
                          << " crc=" << (block.crcValid ? "ok" : "fail")
                          << " corrected=" << block.correctedDibitErrors;
                if (!block.bytes.empty()) {
                    std::cout << " raw=" << p25BytesToHex(block.bytes).toStdString();
                }
                std::cout << "\n";
            }
            if (result.rawTsbkBlocks.size() > 6) {
                std::cout << "  candidate TSBK list truncated at 6 rows\n";
            }
        }
        auto talkgroups = loadP25Talkgroups();
        bool changed = false;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (const auto& block : result.rawTsbkBlocks) {
            if (!block.fecDecoded || !block.crcValid) continue;
            const auto rawHex = p25BytesToHex(block.bytes).toStdString();
            std::cout << "  trusted TSBK bit=" << block.bitOffset
                      << " corrected=" << block.correctedDibitErrors
                      << " raw=" << rawHex << "\n";
            const bool registryEligible = block.correctedDibitErrors <= kP25RegistryMaxCorrectedDibits;
            bool acceptedHighCorrectionGrant = false;
            const auto events = analyzer.ingestTsbk(block.bytes);
            for (const auto& ev : events) {
                std::cout << "    " << p25EventLogText(ev).toStdString() << "\n";
                if (p25ControlEventIsVoiceGrant(ev)) {
                    ++controlVoiceGrantEvents;
                    if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                    else ++controlUnresolvedVoiceGrantEvents;
                    std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                } else if (ev.type == P25ControlEventType::IdentifierUpdate && ev.phase2Candidate) {
                    std::cout << "    TDMA identifier table update: " << p25EventLogText(ev).toStdString() << "\n";
                }
                const bool eventRegistryEligible = p25TsbkEventRegistryEligible(block.correctedDibitErrors, ev);
                if (eventRegistryEligible) {
                    if (p25ControlEventIsVoiceGrant(ev) && !p25ControlEventIsResolvedVoiceGrant(ev)) {
                        p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, block.correctedDibitErrors, nowMs);
                    }
                    changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
                    if (!registryEligible && p25ControlEventIsResolvedVoiceGrant(ev)) {
                        acceptedHighCorrectionGrant = true;
                        std::cout << "    note: accepted resolved voice grant despite "
                                  << block.correctedDibitErrors
                                  << " corrected dibits (voice grant threshold "
                                  << kP25VoiceGrantMaxCorrectedDibits << ")\n";
                    }
                    if (ev.type == P25ControlEventType::IdentifierUpdate &&
                        p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
                        for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                            ++controlResolvedVoiceGrantEvents;
                            std::cout << "    pending-resolved after identifier ID "
                                      << static_cast<int>(ev.identifier) << ": "
                                      << p25EventLogText(resolved).toStdString() << "\n";
                            std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                            changed = mergeP25TalkgroupEvent(talkgroups, targetHz, resolved, nowMs) || changed;
                        }
                    }
                }
                if (!eventRegistryEligible &&
                    p25TsbkSessionIdentifierEligible(block.correctedDibitErrors, ev)) {
                    std::cout << "    note: kept high-correction identifier ID "
                              << static_cast<int>(ev.identifier)
                              << " in the current replay session for pending grant resolution; corrected="
                              << block.correctedDibitErrors
                              << " session threshold=" << kP25SessionIdentifierMaxCorrectedDibits << "\n";
                    for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                        ++controlResolvedVoiceGrantEvents;
                        std::cout << "    pending-resolved after session identifier ID "
                                  << static_cast<int>(ev.identifier) << ": "
                                  << p25EventLogText(resolved).toStdString() << "\n";
                        std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                        changed = mergeP25TalkgroupEvent(talkgroups, targetHz, resolved, nowMs) || changed;
                    }
                }
                if (!eventRegistryEligible &&
                    p25TsbkPendingVoiceGrantEligible(block.correctedDibitErrors, ev)) {
                    p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, block.correctedDibitErrors, nowMs);
                    std::cout << "    note: queued near-threshold unresolved voice grant pending identifier resolution; corrected="
                              << block.correctedDibitErrors
                              << " pending threshold=" << kP25PendingVoiceGrantMaxCorrectedDibits << "\n";
                }
            }
            if (!registryEligible && !acceptedHighCorrectionGrant) {
                std::cout << "    note: CRC valid but corrected dibits exceed registry threshold; only resolved voice grants up to "
                          << kP25VoiceGrantMaxCorrectedDibits
                          << " corrected dibits are saved/followed; unresolved voice grants are only queued up to "
                          << kP25PendingVoiceGrantMaxCorrectedDibits << " corrected dibits\n";
            }
        }
        if (changed) {
            saveP25Talkgroups(talkgroups);
            std::cout << "  Talkgroup registry updated from trusted TSBK.\n";
        }
    }

    if (!result.phase1Pdus.empty()) {
        auto talkgroups = loadP25Talkgroups();
        bool changed = false;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (const auto& pdu : result.phase1Pdus) {
            std::cout << "  P25P1 PDU bit=" << pdu.bitOffset
                      << " format=" << static_cast<int>(pdu.format)
                      << " vendor=0x" << std::hex << static_cast<int>(pdu.vendor)
                      << " op=0x" << static_cast<int>(pdu.opcode) << std::dec
                      << " btf=" << static_cast<int>(pdu.blocksToFollow)
                      << " blocks=" << pdu.dataBlocks.size()
                      << " crc=" << (pdu.headerCrcValid ? "ok" : "fail")
                      << " corr=" << pdu.headerCorrectedDibitErrors
                      << " hdr=" << p25BytesToHex(pdu.headerBytes).toStdString() << "\n";
            if (!pdu.headerFecDecoded || !pdu.headerCrcValid) continue;
            std::vector<std::vector<uint8_t>> dataBlocks;
            dataBlocks.reserve(pdu.dataBlocks.size());
            for (const auto& block : pdu.dataBlocks) dataBlocks.push_back(block.bytes);
            const auto events = analyzer.ingestPhase1Pdu(pdu.format, pdu.vendor, pdu.opcode, pdu.headerBytes, dataBlocks, pdu.headerCrcValid);
            for (const auto& ev : events) {
                std::cout << "    " << p25EventLogText(ev).toStdString() << "\n";
                if (p25ControlEventIsVoiceGrant(ev)) {
                    ++controlVoiceGrantEvents;
                    if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                    else ++controlUnresolvedVoiceGrantEvents;
                    std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                    if (!p25ControlEventIsResolvedVoiceGrant(ev)) {
                        p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, 0, nowMs);
                    }
                }
                changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
            }
        }
        if (changed) {
            saveP25Talkgroups(talkgroups);
            std::cout << "  Talkgroup registry updated from trusted Phase 1 PDU.\n";
        }
    }

    if (!result.imbeFrames.empty()) {
        size_t valid = 0;
        for (const auto& frame : result.imbeFrames) if (frame.valid) ++valid;
        std::cout << "  IMBE voice frames=" << result.imbeFrames.size()
                  << " valid=" << valid
                  << " (mbelib backend=" << (result.stats.voiceBackendAvailable ? "available" : "missing") << ")\n";
    }

    if (!result.phase2Bursts.empty()) {
        for (const auto& burst : result.phase2Bursts) {
            std::ostringstream isch;
            if (!burst.isch.valid) {
                isch << "-";
            } else if (burst.isch.sync) {
                isch << "sync(err=" << burst.isch.errors << ")";
            } else {
                isch << "ch=" << static_cast<int>(burst.isch.channel)
                     << ",loc=" << static_cast<int>(burst.isch.location)
                     << ",fa=" << (burst.isch.freeAccess ? "yes" : "no")
                     << ",cnt=" << static_cast<int>(burst.isch.ultraframeCounter)
                     << ",err=" << burst.isch.errors;
            }
            std::string macEvent = "-";
            if (burst.macPttSeen) macEvent = "ptt";
            else if (burst.macActiveSeen) macEvent = "active";
            else if (burst.macEndPttSeen) macEvent = "end-ptt";
            else if (burst.macIdleSeen) macEvent = "idle";
            else if (burst.macHangtimeSeen) macEvent = "hangtime";
            std::cout << "  P25P2 burst dibit=" << burst.dibitOffset
                      << " kind=" << P25LiveDecoder::phase2BurstKindToString(burst.kind)
                       << " duid=0x" << std::hex << burst.duid << std::dec
                       << " duidErr=" << burst.duidErrors
                       << " syncErr=" << burst.syncErrors
                       << " syncAdj=" << (burst.syncOffsetAdjusted ? std::to_string(burst.syncOffsetDibits) : std::string("0"))
                       << " vcw=" << burst.voiceCodewords.size()
                       << " tdmaSync=" << (burst.tdmaSyncLock ? "yes" : "no")
                      << " sf=" << (burst.superframeLocked ? "locked" : "no")
                      << " sfScore=" << burst.superframeSyncScore
                      << " legacyAudioLock=" << (burst.phase2AudioLock ? "yes" : "no")
                      << " sessionRelease=" << (burst.sessionAudioRelease ? "yes" : "no")
                      << " sfBurst=" << (burst.superframeBurstIndexKnown ? std::to_string(burst.superframeBurstIndex) : std::string("-"))
                      << " grantSlot=" << (burst.grantSlotKnown ? std::to_string(burst.grantSlot) : std::string("-"))
                      << " xorMask=" << (burst.xorMaskApplied ? "yes" : "not-yet")
                      << " maskPhase=" << (burst.xorMaskPhaseKnown ? std::to_string(burst.xorMaskPhase) : std::string("-"))
                      << " phaseScore=" << burst.xorMaskPhaseScore
                      << " mac=" << (burst.macCrcValid ? "crc-ok" : (burst.macFecDecoded ? "fec-only" : "-"))
                      << " macEvent=" << macEvent
                      << " ess=" << (burst.essKnown ? (burst.encrypted ? "encrypted" : "clear") : "unknown")
                      << " isch=" << isch.str() << "\n";
        }
        const bool lateEntry = std::any_of(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& burst) {
            return !burst.sessionAudioRelease &&
                   !burst.voiceCodewords.empty() &&
                   burst.xorMaskApplied &&
                   !burst.macCrcValid;
        });
        if (lateEntry) {
            std::cout << "  note: Phase 2 late entry: voice bursts present, mask applied, waiting for MAC CRC/ESS before audio release.\n";
        }
                std::cout << "  note: Phase 2 AMBE audio follows target-slot security; grants queue until target-slot PTT/ESS or MAC/ESS proves clear. Explicit encrypted state stays muted.\n";
    }

    if (!result.phase2MacPdus.empty()) {
        auto talkgroups = loadP25Talkgroups();
        bool changed = false;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (const auto& pdu : result.phase2MacPdus) {
            std::cout << "  P25P2 MAC type=" << p25Phase2MacPduTypeToString(pdu.opcode)
                      << " offset=" << static_cast<int>(pdu.offset)
                      << " source=" << P25LiveDecoder::phase2BurstKindToString(pdu.source)
                      << " crc=" << (pdu.crcValid ? "ok" : "fail")
                      << " corr=" << pdu.correctedSymbols
                      << " " << p25Phase2MacPduHypothesisText(pdu).toStdString()
                      << " raw=" << p25BytesToHex(pdu.bytes).toStdString() << "\n";
            const auto events = analyzer.ingestPhase2MacPdu(
                pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid, pdu.macStructureMaxBits);
            for (const auto& ev : events) {
                std::cout << "    " << p25EventLogText(ev).toStdString() << "\n";
                if (p25ControlEventIsVoiceGrant(ev)) {
                    ++controlVoiceGrantEvents;
                    if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                    else ++controlUnresolvedVoiceGrantEvents;
                    std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                    if (!p25ControlEventIsResolvedVoiceGrant(ev)) {
                        p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, 0, nowMs);
                    }
                }
                changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
                if (ev.type == P25ControlEventType::IdentifierUpdate &&
                    p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
                    for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                        ++controlResolvedVoiceGrantEvents;
                        std::cout << "    pending-resolved after Phase 2 MAC identifier ID "
                                  << static_cast<int>(ev.identifier) << ": "
                                  << p25EventLogText(resolved).toStdString() << "\n";
                        std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                        changed = mergeP25TalkgroupEvent(talkgroups, targetHz, resolved, nowMs) || changed;
                    }
                }
            }
        }
        if (changed) {
            saveP25Talkgroups(talkgroups);
            std::cout << "  Talkgroup registry updated from trusted Phase 2 MAC.\n";
        }
    }

    if (trustedTsbk > 0 || trustedPhase1Pdu > 0 || trustedPhase2Mac > 0 || result.stats.phase2Bursts > 0) {
        std::cout << "  control audit: stage=" << lockStage.toStdString()
                  << " trustedTsbk=" << trustedTsbk
                  << " trustedP1Pdu=" << trustedPhase1Pdu
                  << " trustedP2Mac=" << trustedPhase2Mac
                  << " voiceGrants=" << controlVoiceGrantEvents
                  << " resolvedGrants=" << controlResolvedVoiceGrantEvents
                  << " unresolvedGrants=" << controlUnresolvedVoiceGrantEvents
                  << " ops=" << p25ControlAuditOpsText(trustedControlOps).toStdString()
                  << "\n";
        if ((trustedTsbk > 0 || trustedPhase2Mac > 0) && controlVoiceGrantEvents == 0) {
            std::cout << "  note: trusted control decode contained no voice-grant opcode in this window; "
                      << "there was nothing eligible for follow to accept.\n";
        }
        if (result.stats.phase2Bursts > 0 && trustedPhase2Mac == 0) {
            std::cout << "  note: Phase 2 burst telemetry is present without CRC-valid MAC; "
                      << "treat this as RF/symbol/framer evidence, not a followable grant.\n";
        }
    }

    for (const auto& warning : result.warnings) {
        std::cout << "  note: " << warning << "\n";
    }
}
