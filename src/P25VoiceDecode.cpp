#include "P25VoiceDecode.h"
#include "P25VoiceSession.h"
#include "P25DecodeConfig.h"
#include "DemodModeUtils.h"

#include "DeviceManager.h"
#include "P25AudioDropClass.h"
#include "P25RollingIq.h"
#include "P25SdrtrunkTune.h"
#include "RemoteDiagnostics.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>

void appendCliP25OppositeWavCapture(const std::vector<float>& samples);
void appendLiveIqSpeakerWavCapture(const float* samples, size_t count);

using json = nlohmann::json;

double defaultBandwidthForMode(DemodMode mode)
{
    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO: return 220000.0;
        case DemodMode::AM: return 20000.0;
        case DemodMode::CW: return 1000.0;
        case DemodMode::USB:
        case DemodMode::LSB: return 6000.0;
        case DemodMode::NFM:
        default: return 12500.0;
    }
}


double defaultLpfForMode(DemodMode mode)
{
    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO: return 15000.0;
        case DemodMode::AM: return 9000.0;
        case DemodMode::CW: return 900.0;
        case DemodMode::USB:
        case DemodMode::LSB: return 3000.0;
        case DemodMode::NFM:
        default: return 3000.0;
    }
}


double lpfForModeAndBandwidth(DemodMode mode, double bandwidthHz)
{
    const double bw = (std::isfinite(bandwidthHz) && bandwidthHz > 0.0)
        ? bandwidthHz
        : defaultBandwidthForMode(mode);

    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO:
            return 15000.0;
        case DemodMode::AM:
            return std::clamp(std::min(9000.0, bw * 0.45), 2500.0, 10000.0);
        case DemodMode::CW:
            return std::clamp(std::min(900.0, bw * 0.90), 300.0, 1200.0);
        case DemodMode::USB:
        case DemodMode::LSB:
            return std::clamp(std::min(3000.0, bw * 0.95), 1800.0, 3600.0);
        case DemodMode::NFM:
        default:
            return std::clamp(std::min(4500.0, bw * 0.35), 2500.0, 5000.0);
    }
}


double snapBandwidthForMode(DemodMode mode, double detectedHz, double tunedFreqHz)
{
    double bw = std::isfinite(detectedHz) && detectedHz > 0.0 ? detectedHz : defaultBandwidthForMode(mode);
    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO:
            // Broadcast FM needs ~200 kHz+ occupied BW; 180 kHz default was
            // crackly in field. Snap into a 180–250 kHz window.
            return std::clamp(bw, 180000.0, 250000.0);
        case DemodMode::AM:
            if (bw <= 8000.0) return 6000.0;
            if (bw <= 14000.0) return 10000.0;
            if (bw <= 18000.0) return 15000.0;
            if (bw <= 26000.0) return 20000.0;
            return std::clamp(bw, 6000.0, 30000.0);
        case DemodMode::USB:
        case DemodMode::LSB:
            return 6000.0;
        case DemodMode::CW:
            if (bw <= 500.0) return 500.0;
            if (bw <= 1200.0) return 1000.0;
            return std::clamp(bw, 500.0, 2000.0);
        case DemodMode::NFM:
        default:
            // Modern CB/PMR/LMR narrowband spacing is commonly 12.5 kHz; support 25 kHz when the signal is visibly wider.
            (void)tunedFreqHz;
            if (bw <= 18000.0) return 12500.0;
            if (bw <= 36000.0) return 25000.0;
            return std::clamp(bw, 12500.0, 50000.0);
    }
}



bool isHfDxPlan(const BandPlanEntry& plan)
{
    return plan.endHz <= 30.0e6;
}


bool hfClassifierCanOverridePlan(const SignalRecommendation& rec,
                                        const BandPlanEntry& plan)
{
    if (rec.confidence < 0.70) return false;
    if (rec.demodMode == plan.mode) return true;
    if (rec.demodMode == DemodMode::CW && rec.confidence >= 0.70) return true;
    if (rec.demodMode == DemodMode::AM && plan.mode == DemodMode::AM) return true;
    if ((rec.demodMode == DemodMode::USB || rec.demodMode == DemodMode::LSB) &&
        (plan.mode == DemodMode::USB || plan.mode == DemodMode::LSB) &&
        rec.confidence >= 0.78) {
        return true;
    }
    return false;
}


SmartModeSelection chooseSmartModeAndBandwidth(const std::vector<float>& powerDb,
                                                      double sampleRateHz,
                                                      double centerFreqHz,
                                                      double tunedFreqHz,
                                                      DemodMode requestedMode,
                                                      const SignalRecommendation* preferredRecommendation)
{
    SmartModeSelection out;
    out.classifier = preferredRecommendation
        ? *preferredRecommendation
        : AdvancedSignalClassifier::instance().classifySpectrum(powerDb, sampleRateHz, centerFreqHz, tunedFreqHz);

    const BandPlanEntry* plan = findBandPlanForFrequency(tunedFreqHz);
    if (requestedMode == DemodMode::AUTO && plan && isHfDxPlan(*plan) &&
        !hfClassifierCanOverridePlan(out.classifier, *plan)) {
        out.mode = plan->mode;
        out.bandwidthHz = plan->bandwidthHz;
        out.lpfHz = plan->lpfHz;
        out.source = plan->name + " plan prior";
        return out;
    }

    if (requestedMode == DemodMode::AUTO && out.classifier.confidence >= 0.70) {
        out.mode = out.classifier.demodMode;
        out.bandwidthHz = out.classifier.standardBandwidthHz;
        out.lpfHz = out.classifier.audioLowPassHz;
        out.source = out.classifier.label + " classifier";
        return out;
    }

    if (plan) {
        if (requestedMode == DemodMode::AUTO || requestedMode == plan->mode) {
            out.mode = plan->mode;
            out.bandwidthHz = plan->bandwidthHz;
            out.lpfHz = plan->lpfHz;
            out.source = plan->name + (out.classifier.confidence > 0.0 ? " plan" : "");
            return out;
        }
    }

    out.mode = requestedMode == DemodMode::AUTO ? out.classifier.demodMode : requestedMode;
    if (out.mode == DemodMode::AUTO) out.mode = DemodMode::NFM;

    if (out.classifier.confidence >= 0.45 && out.classifier.demodMode == out.mode) {
        out.bandwidthHz = out.classifier.standardBandwidthHz;
        out.lpfHz = out.classifier.audioLowPassHz;
        out.source = out.classifier.label + " classifier";
    } else {
        const double searchHz = (out.mode == DemodMode::WFM) ? 300000.0 : 50000.0;
        const double detected = detectChannelBandwidthAround(powerDb, sampleRateHz, centerFreqHz, tunedFreqHz, searchHz);
        out.bandwidthHz = snapBandwidthForMode(out.mode, detected, tunedFreqHz);
        out.lpfHz = lpfForModeAndBandwidth(out.mode, out.bandwidthHz);
        out.source = "signal fallback";
    }
    return out;
}


double percentileDb(std::vector<double> values, double percentile, double fallback)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
        return !std::isfinite(v);
    }), values.end());
    if (values.empty()) return fallback;
    percentile = std::clamp(percentile, 0.0, 1.0);
    size_t idx = static_cast<size_t>(std::llround(percentile * (values.size() - 1)));
    idx = std::min(idx, values.size() - 1);
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
    return values[idx];
}


double linearPowerDb(double db)
{
    if (!std::isfinite(db)) return 0.0;
    db = std::clamp(db, -160.0, 80.0);
    constexpr double kMinDb = -160.0;
    constexpr double kStepDb = 0.25;
    constexpr size_t kLutSize = 961;
    static const std::array<double, kLutSize> lut = [] {
        std::array<double, kLutSize> table{};
        for (size_t i = 0; i < table.size(); ++i) {
            table[i] = std::pow(10.0, (kMinDb + static_cast<double>(i) * kStepDb) / 10.0);
        }
        return table;
    }();
    const double pos = (db - kMinDb) / kStepDb;
    const size_t lo = std::min(static_cast<size_t>(pos), kLutSize - 1);
    const size_t hi = std::min(lo + 1, kLutSize - 1);
    const double frac = pos - static_cast<double>(lo);
    return lut[lo] * (1.0 - frac) + lut[hi] * frac;
}


double topLinearAverageDb(std::vector<double> values, size_t topCount, double fallback)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
        return !std::isfinite(v);
    }), values.end());
    if (values.empty()) return fallback;
    topCount = std::clamp<size_t>(topCount, 1, values.size());
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(topCount - 1), values.end(), std::greater<double>());
    double linear = 0.0;
    for (size_t i = 0; i < topCount; ++i) {
        linear += linearPowerDb(values[i]);
    }
    linear /= static_cast<double>(topCount);
    return 10.0 * std::log10(std::max(linear, 1e-20));
}


RfSquelchMetrics computeRfSquelchMetrics(const std::vector<float>& powerDb,
                                                double sampleRateHz,
                                                double centerFreqHz,
                                                double targetFreqHz,
                                                double channelBwHz,
                                                DemodMode mode)
{
    RfSquelchMetrics out;
    const int bins = static_cast<int>(powerDb.size());
    if (bins < 8 || sampleRateHz <= 0.0 || !std::isfinite(sampleRateHz)) return out;

    double bw = (channelBwHz > 0.0 && std::isfinite(channelBwHz)) ? channelBwHz : defaultBandwidthForMode(mode);
    bw = std::clamp(bw, sampleRateHz / bins, sampleRateHz);

    const double binHz = sampleRateHz / static_cast<double>(bins);
    const double fullStart = centerFreqHz - sampleRateHz / 2.0;
    const double rel = (targetFreqHz - fullStart) / binHz;
    if (!std::isfinite(rel) || rel < -2.0 || rel > bins + 2.0) return out;

    const int centerBin = std::clamp(static_cast<int>(std::llround(rel - 0.5)), 0, bins - 1);
    const int halfChannelBins = std::max(2, static_cast<int>(std::ceil((bw * 0.5) / binHz)));

    double localHalfHz = std::max(bw * 4.0, 50000.0);
    if (mode == DemodMode::WFM || mode == DemodMode::AUTO) localHalfHz = std::max(bw * 1.6, 260000.0);
    if (mode == DemodMode::USB || mode == DemodMode::LSB) localHalfHz = std::max(bw * 6.0, 20000.0);
    if (mode == DemodMode::CW) localHalfHz = std::max(bw * 8.0, 8000.0);
    localHalfHz = std::min(localHalfHz, sampleRateHz * 0.5);
    const int halfLocalBins = std::max(halfChannelBins + 2, static_cast<int>(std::ceil(localHalfHz / binHz)));

    std::vector<double> local;
    local.reserve(static_cast<size_t>(halfLocalBins * 2 + 1));
    for (int i = std::max(0, centerBin - halfLocalBins); i <= std::min(bins - 1, centerBin + halfLocalBins); ++i) {
        local.push_back(powerDb[static_cast<size_t>(i)]);
    }
    if (local.size() < 8) {
        local.clear();
        for (float v : powerDb) local.push_back(v);
    }

    std::vector<double> channel;
    channel.reserve(static_cast<size_t>(halfChannelBins * 2 + 1));
    for (int i = std::max(0, centerBin - halfChannelBins); i <= std::min(bins - 1, centerBin + halfChannelBins); ++i) {
        channel.push_back(powerDb[static_cast<size_t>(i)]);
    }
    if (channel.empty()) return out;

    // Lower-percentile floor is taken from the receiver channel window itself, so squelch
    // follows the selected/auto BW around the tuned frequency rather than the whole display.
    const auto& floorBins = channel.size() >= 8 ? channel : local;
    out.noiseFloorDb = percentileDb(floorBins, 0.20, -120.0);

    // Top-bin linear average catches AM/NFM carriers without letting one unstable bin dominate.
    const size_t topCount = std::max<size_t>(1, std::min<size_t>(6, channel.size() / 20 + 1));
    out.signalLevelDb = topLinearAverageDb(channel, topCount, percentileDb(channel, 0.90, out.noiseFloorDb));
    out.snrDb = out.signalLevelDb - out.noiseFloorDb;
    out.valid = std::isfinite(out.signalLevelDb) && std::isfinite(out.noiseFloorDb);
    return out;
}


double applyNfmAfcFromSpectrum(Receiver& rx,
                                      const std::vector<float>& powerDb,
                                      double sampleRateHz,
                                      double centerFreqHz,
                                      double nominalFreqHz,
                                      double channelBwHz,
                                      DemodMode mode)
{
    const bool narrowFm = (mode == DemodMode::NFM) || (mode == DemodMode::AUTO && channelBwHz <= 50000.0);
    if (rx.p25AfcFrozen) {
        const double frozen = rx.p25FrozenAfcOffsetHz;
        gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(frozen, nominalFreqHz));
        gLastAfcConfidence.store(1.0);
        return nominalFreqHz + frozen;
    }
    if (!rx.afcEnabled || !narrowFm || channelBwHz <= 0.0 || channelBwHz > 60000.0 || powerDb.empty()) {
        rx.afcLocked = false;
        rx.afcOffsetHz = 0.0;
        gLastAfcPpmDelta.store(std::numeric_limits<double>::quiet_NaN());
        gLastAfcConfidence.store(0.0);
        gLastAfcBinHz.store(0.0);
        return nominalFreqHz;
    }

    const double searchHz = std::clamp(std::max(18000.0, channelBwHz * 1.6), 8000.0, 45000.0);
    const double maxSignalBw = std::clamp(channelBwHz * 1.35, 8000.0, 60000.0);
    const auto estimate = estimateSignalOffsetFromSpectrum(powerDb, sampleRateHz, centerFreqHz,
                                                           nominalFreqHz, searchHz, maxSignalBw);
    if (estimate.valid) {
        const double alpha = rx.afcLocked ? 0.18 : 1.0;
        const double proposedOffsetHz = rx.afcOffsetHz * (1.0 - alpha) + estimate.offsetHz * alpha;
        // Keep live AFC from chasing bursty adjacent-channel energy. P25 control/voice
        // decoding wants a stable channel center; large block-to-block jumps should be
        // learned over several windows, not applied instantly.
        const double maxStepHz = rx.afcLocked ? std::max(250.0, std::min(1200.0, estimate.binHz * 2.0)) : searchHz;
        const double deltaHz = std::clamp(proposedOffsetHz - rx.afcOffsetHz, -maxStepHz, maxStepHz);
        rx.afcOffsetHz = std::clamp(rx.afcOffsetHz + deltaHz, -searchHz, searchHz);
        rx.afcLocked = true;
        gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(rx.afcOffsetHz, nominalFreqHz));
        gLastAfcConfidence.store(estimate.confidence);
        gLastAfcBinHz.store(estimate.binHz);
    } else if (rx.afcLocked) {
        rx.afcOffsetHz *= 0.92;
        if (std::abs(rx.afcOffsetHz) < 25.0) {
            rx.afcOffsetHz = 0.0;
            rx.afcLocked = false;
            gLastAfcPpmDelta.store(std::numeric_limits<double>::quiet_NaN());
            gLastAfcConfidence.store(0.0);
            gLastAfcBinHz.store(0.0);
        } else {
            gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(rx.afcOffsetHz, nominalFreqHz));
            gLastAfcConfidence.store(0.0);
        }
    } else {
        gLastAfcPpmDelta.store(std::numeric_limits<double>::quiet_NaN());
        gLastAfcConfidence.store(0.0);
        gLastAfcBinHz.store(estimate.binHz);
    }

    return nominalFreqHz + rx.afcOffsetHz;
}


bool p25MaybeAutoApplyPpmFromControlAfc(size_t deviceIndex,
                                        double controlFreqHz,
                                        double afcOffsetHz,
                                        double afcConfidence,
                                        qint64 nowMs,
                                        QString* logLine)
{
    if (logLine) logLine->clear();
    if (!std::isfinite(controlFreqHz) || controlFreqHz < 1.0e6) return false;
    if (!p25AutoPpmAfcSampleAcceptable(afcOffsetHz, afcConfidence)) return false;

    const long long lastApply = gP25LastAutoPpmApplyMs.load(std::memory_order_relaxed);
    if (lastApply > 0 && nowMs >= lastApply && (nowMs - lastApply) < kP25AutoPpmCooldownMs) {
        return false;
    }

    const double deltaPpm = estimatePpmCorrectionDelta(afcOffsetHz, controlFreqHz);
    if (!std::isfinite(deltaPpm) || std::abs(deltaPpm) < kP25AutoPpmMinAbsDelta) return false;

    auto& mgr = DeviceManager::instance();
    const auto devices = mgr.getDevices();
    if (deviceIndex >= devices.size()) return false;
    const double currentPpm = devices[deviceIndex].frequencyCorrectionPpm;
    const double stepped = std::clamp(deltaPpm, -kP25AutoPpmMaxStep, kP25AutoPpmMaxStep);
    const double suggested = std::clamp(currentPpm + stepped, -200.0, 200.0);
    if (std::abs(suggested - currentPpm) < kP25AutoPpmMinAbsDelta) return false;

    mgr.setFrequencyCorrection(deviceIndex, suggested);
    gP25LastAutoPpmApplyMs.store(nowMs, std::memory_order_relaxed);
    gP25LastAutoPpmValue.store(suggested, std::memory_order_relaxed);
    if (logLine) {
        *logLine = QString("Auto PPM: CC AFC=%1Hz conf=%2 → device %3 ppm %4 → %5 (delta=%6). Applied on return-to-control only.")
            .arg(afcOffsetHz, 0, 'f', 1)
            .arg(afcConfidence, 0, 'f', 2)
            .arg(static_cast<qulonglong>(deviceIndex))
            .arg(currentPpm, 0, 'f', 2)
            .arg(suggested, 0, 'f', 2)
            .arg(stepped, 0, 'f', 2);
    }
    return true;
}


void p25Phase2ResetTrafficTargetOffset(Receiver& rx) noexcept;
double p25Phase2EffectiveTrafficTargetOffsetHz(const Receiver& rx) noexcept;

double p25VoiceAfcTargetHz(const Receiver& rx, double nominalFreqHz, double channelBwHz)
{
    if (rx.p25VoicePhase2) {
        const double trafficOffsetHz = p25Phase2EffectiveTrafficTargetOffsetHz(rx);
        const bool verifiedTrafficOffset =
            rx.p25Phase2TrafficTargetOffsetKnown &&
            rx.p25Phase2TrafficTargetOffsetTrust >= kP25Phase2TrafficTargetOffsetVerifiedTrust &&
            rx.p25Phase2TrafficTargetOffsetMisses == 0 &&
            trafficOffsetHz != 0.0;
        // After a one-RTL physical retune the tuner is on the granted voice MHz.
        // Control-channel AFC is not a reliable traffic offset; field captures
        // showed voicetest at offset=0 finding p2bursts while live +CC-AFC saw zero.
        if (rx.p25TrafficRetunesPrimary && rx.p25IndependentTrafficSource) {
            if (verifiedTrafficOffset) {
                gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(trafficOffsetHz, nominalFreqHz));
                gLastAfcConfidence.store(1.0);
                gLastAfcBinHz.store(trafficOffsetHz);
                return nominalFreqHz + trafficOffsetHz;
            }
            gLastAfcPpmDelta.store(0.0);
            gLastAfcConfidence.store(0.0);
            gLastAfcBinHz.store(0.0);
            return nominalFreqHz;
        }
        if (rx.p25TrafficRetunesPrimary && verifiedTrafficOffset) {
            const double seededHz = nominalFreqHz + trafficOffsetHz;
            gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(trafficOffsetHz, nominalFreqHz));
            gLastAfcConfidence.store(1.0);
            gLastAfcBinHz.store(trafficOffsetHz);
            return seededHz;
        }
        gLastAfcPpmDelta.store(0.0);
        gLastAfcConfidence.store(rx.p25AfcFrozen ? 0.5 : 0.0);
        gLastAfcBinHz.store(0.0);
        return nominalFreqHz;
    }
    if (!rx.p25AfcFrozen || !std::isfinite(rx.p25FrozenAfcOffsetHz)) return nominalFreqHz;
    const double halfChannelHz = std::isfinite(channelBwHz) && channelBwHz > 0.0
        ? channelBwHz * 0.5
        : 6250.0;
    const double limitHz = std::clamp(halfChannelHz * 0.72, 1200.0, 5000.0);
    const double frozenHz = std::clamp(rx.p25FrozenAfcOffsetHz, -limitHz, limitHz);
    gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(frozenHz, nominalFreqHz));
    gLastAfcConfidence.store(1.0);
    gLastAfcBinHz.store(frozenHz);
    return nominalFreqHz + frozenHz;
}



void p25Phase2NoteCadenceWindow(const P25VoiceAudioBlock& out) noexcept
{
    gP25Phase2Cadence.windows.fetch_add(1, std::memory_order_relaxed);
    gP25Phase2Cadence.vcw.fetch_add(static_cast<long long>(out.phase2VoiceCodewords), std::memory_order_relaxed);
    gP25Phase2Cadence.targetVcw.fetch_add(static_cast<long long>(out.phase2TargetVoiceCodewords), std::memory_order_relaxed);
    gP25Phase2Cadence.fed.fetch_add(static_cast<long long>(out.phase2FedToMbelib), std::memory_order_relaxed);
    gP25Phase2Cadence.emitted.fetch_add(static_cast<long long>(out.phase2EmittedPcmFrames), std::memory_order_relaxed);
    gP25Phase2Cadence.dups.fetch_add(static_cast<long long>(out.phase2DuplicateSuppressedVoiceCodewords), std::memory_order_relaxed);
    gP25Phase2Cadence.gaps.fetch_add(static_cast<long long>(out.phase2FeedGaps), std::memory_order_relaxed);
    gP25Phase2Cadence.reject.fetch_add(static_cast<long long>(out.phase2RejectedVoiceCodewords), std::memory_order_relaxed);
}


uint8_t p25Phase2BurstVoiceCount(P25Phase2BurstKind kind) noexcept
{
    if (kind == P25Phase2BurstKind::Voice2) return 2;
    if (kind == P25Phase2BurstKind::Voice4) return 4;
    return 0;
}




ReceiverSessionKey p25ReceiverSessionKey(const Receiver& rx)
{
    return {&rx, rx.p25TrafficSessionGeneration.load(std::memory_order_acquire)};
}



int64_t p25RoundFrequencyHz(double hz) noexcept
{
    return std::isfinite(hz) && hz > 0.0 ? static_cast<int64_t>(std::llround(hz)) : 0;
}


P25P2CallAudioKey p25CurrentPhase2AudioKey(const Receiver& rx, double targetFreqHz) noexcept
{
    P25P2CallAudioKey key;
    key.nac = rx.p25VoiceNac;
    key.wacn = rx.p25VoiceWacn;
    key.systemId = rx.p25VoiceSystemId;
    key.talkgroupId = rx.p25VoiceTalkgroupId;
    key.sourceId = rx.p25VoiceSourceId;
    key.callSessionId = rx.p25CurrentCallSessionId;
    key.grantEpochMs = rx.p25VoiceGrantEpochMs;
    key.slot = rx.p25VoiceTdmaSlotKnown ? static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) : 0xffu;
    key.frequencyHz = p25RoundFrequencyHz(targetFreqHz);
    return key;
}


void p25ClearPhase2RecentSecurityEvidence(Receiver& rx) noexcept
{
    rx.p25Phase2RecentSecurityTalkgroupId = 0;
    rx.p25Phase2RecentSecuritySourceId = 0;
    rx.p25Phase2RecentSecurityCallSessionId = 0;
    rx.p25Phase2RecentSecuritySlot = 0xffu;
    rx.p25Phase2RecentSecurityFrequencyHz = 0;
    rx.p25Phase2RecentSecurityGrantEpochMs = 0;
    rx.p25Phase2RecentSecurityEvidenceMs = 0;
    rx.p25Phase2RecentTargetMacCrcValid = false;
    rx.p25Phase2RecentAnyMacCrcValid = false;
    rx.p25Phase2RecentTargetEssKnown = false;
    rx.p25Phase2RecentTargetEssEncrypted = false;
    rx.p25Phase2RecentTargetSessionAudioRelease = false;
    rx.p25Phase2RecentTargetSecurityStateFromPtt = false;
    rx.p25Phase2RecentSuperframeMaskLock = false;
}


bool p25Phase2RecentSecurityEvidenceMatches(const Receiver& rx,
                                                   const P25P2CallAudioKey& key) noexcept
{
    if (!key.valid()) return false;
    const bool sourceCompatible =
        rx.p25Phase2RecentSecuritySourceId == 0 ||
        key.sourceId == 0 ||
        rx.p25Phase2RecentSecuritySourceId == key.sourceId;
    return rx.p25Phase2RecentSecurityTalkgroupId == key.talkgroupId &&
        sourceCompatible &&
        rx.p25Phase2RecentSecurityCallSessionId == key.callSessionId &&
        rx.p25Phase2RecentSecuritySlot == key.slot &&
        rx.p25Phase2RecentSecurityFrequencyHz == key.frequencyHz &&
        rx.p25Phase2RecentSecurityGrantEpochMs == key.grantEpochMs;
}


void p25Phase2AdoptGrantSourceIdForCurrentCall(Receiver& rx,
                                                      uint32_t sourceId) noexcept
{
    if (sourceId == 0 || rx.p25VoiceSourceId == sourceId) return;
    const uint32_t priorSourceId = rx.p25VoiceSourceId;
    rx.p25VoiceSourceId = sourceId;
    if (priorSourceId != 0) return;

    const uint64_t callSessionId = rx.p25CurrentCallSessionId;
    if (callSessionId == 0) return;
    auto adoptQueue = [&](P25P2PendingAudioQueue& queue) noexcept {
        if (queue.armed &&
            queue.key.callSessionId == callSessionId &&
            queue.key.sourceId == 0) {
            queue.key.sourceId = sourceId;
        }
    };
    adoptQueue(rx.p25SessionState.pendingAudio);
    adoptQueue(rx.p25SessionState.pendingAudioOpposite);
    if (rx.p25SessionState.ambeDedupe.callSessionId == callSessionId &&
        rx.p25SessionState.ambeDedupe.sourceId == 0) {
        rx.p25SessionState.ambeDedupe.sourceId = sourceId;
    }
    if (rx.p25Phase2RecentSecurityCallSessionId == callSessionId &&
        rx.p25Phase2RecentSecuritySourceId == 0) {
        rx.p25Phase2RecentSecuritySourceId = sourceId;
    }
}


bool p25Phase2RecentSecurityEvidenceUsable(const Receiver& rx,
                                                  const P25P2CallAudioKey& key,
                                                  qint64 nowMs) noexcept
{
    if (!p25Phase2RecentSecurityEvidenceMatches(rx, key)) return false;
    if (rx.p25Phase2RecentSecurityEvidenceMs <= 0) return false;
    return nowMs - rx.p25Phase2RecentSecurityEvidenceMs <= kP25Phase2RecentSecurityEvidenceTtlMs;
}


void p25BindPhase2RecentSecurityEvidenceToCall(Receiver& rx,
                                                      const P25P2CallAudioKey& key) noexcept
{
    if (!key.valid()) return;
    if (p25Phase2RecentSecurityEvidenceMatches(rx, key)) return;
    p25ClearPhase2RecentSecurityEvidence(rx);
    rx.p25Phase2RecentSecurityTalkgroupId = key.talkgroupId;
    rx.p25Phase2RecentSecuritySourceId = key.sourceId;
    rx.p25Phase2RecentSecurityCallSessionId = key.callSessionId;
    rx.p25Phase2RecentSecuritySlot = key.slot;
    rx.p25Phase2RecentSecurityFrequencyHz = key.frequencyHz;
    rx.p25Phase2RecentSecurityGrantEpochMs = key.grantEpochMs;
}


void p25RefreshPhase2RecentSecurityEvidence(Receiver& rx,
                                                   const P25P2CallAudioKey& key,
                                                   qint64 nowMs,
                                                   bool targetMacCrc,
                                                   bool anyMacCrc,
                                                   bool targetEssKnown,
                                                   bool targetEssEncrypted,
                                                   bool targetSessionAudioRelease,
                                                   bool targetSecurityStateFromPtt,
                                                   bool superframeMaskLock) noexcept
{
    if (!key.valid()) return;
    p25BindPhase2RecentSecurityEvidenceToCall(rx, key);
    // Only refresh the 12 s TTL on real security or MAC evidence.  Superframe/
    // mask lock alone used to keep sticky clear/enc alive through dual-slot
    // windows without new PTT/ESS (audit P1-3).
    const bool securityEvidenceThisWindow =
        targetMacCrc || anyMacCrc || targetEssKnown ||
        (targetSessionAudioRelease && !targetEssEncrypted);
    if (securityEvidenceThisWindow) {
        rx.p25Phase2RecentSecurityEvidenceMs = nowMs;
    }
    rx.p25Phase2RecentTargetMacCrcValid = rx.p25Phase2RecentTargetMacCrcValid || targetMacCrc;
    rx.p25Phase2RecentAnyMacCrcValid = rx.p25Phase2RecentAnyMacCrcValid || anyMacCrc;
    rx.p25Phase2RecentTargetEssKnown = rx.p25Phase2RecentTargetEssKnown || targetEssKnown;
    // DEC-0059: this-window target clear must clear sticky encrypted. A single
    // false/companion-attributed encrypted observation used to OR-latch for the
    // full TTL and re-poison every later hop (145139 clear→ReturnEncrypted).
    if (targetEssKnown && !targetEssEncrypted) {
        rx.p25Phase2RecentTargetEssEncrypted = false;
    } else if (targetEssKnown && targetEssEncrypted) {
        rx.p25Phase2RecentTargetEssEncrypted = true;
    }
    const bool targetClearSessionAudioRelease =
        targetSessionAudioRelease && !targetEssEncrypted;
    rx.p25Phase2RecentTargetSessionAudioRelease =
        rx.p25Phase2RecentTargetSessionAudioRelease || targetClearSessionAudioRelease;
    rx.p25Phase2RecentTargetSecurityStateFromPtt =
        rx.p25Phase2RecentTargetSecurityStateFromPtt || targetSecurityStateFromPtt;
    rx.p25Phase2RecentSuperframeMaskLock =
        rx.p25Phase2RecentSuperframeMaskLock || superframeMaskLock;
}


bool p25Phase2ValidationLoggingEnabled();
bool p25Phase2DeepTraceEnabled();

// Reasons that may clear pending AMBE/PCM.  Transient per-window decode/gate
// failures must NOT clear once the call is established clear.

const char* p25PendingClearReasonName(P25PendingClearReason reason) noexcept
{
    switch (reason) {
    case P25PendingClearReason::CallIdentityChanged: return "call-identity";
    case P25PendingClearReason::EncryptedState: return "encrypted";
    case P25PendingClearReason::CallEnded: return "call-end";
    case P25PendingClearReason::RetuneOrGeneration: return "retune-generation";
    case P25PendingClearReason::UserStop: return "user-stop";
    case P25PendingClearReason::SlotProbeDestructive: return "slot-probe";
    case P25PendingClearReason::PttStartReset: return "ptt-start";
    case P25PendingClearReason::LiveStreamPreferred: return "live-stream-preferred";
    }
    return "unknown";
}


bool p25Phase2SpeakerPlaybackClearAllowed(P25PendingClearReason reason) noexcept
{
    switch (reason) {
    case P25PendingClearReason::CallIdentityChanged:
    case P25PendingClearReason::EncryptedState:
    case P25PendingClearReason::CallEnded:
    case P25PendingClearReason::RetuneOrGeneration:
    case P25PendingClearReason::UserStop:
    case P25PendingClearReason::SlotProbeDestructive:
        return true;
    case P25PendingClearReason::PttStartReset:
    case P25PendingClearReason::LiveStreamPreferred:
        return false;
    }
    return false;
}


P25Phase2SpeakerPendingQueue& p25SpeakerPendingFor(P25SpeakerPendingMap& map,
                                                            const Receiver& rx)
{
    auto& queue = map[p25ReceiverSessionKey(rx)];
    if (queue.callSessionId == 0 && rx.p25CurrentCallSessionId != 0) {
        queue.callSessionId = rx.p25CurrentCallSessionId;
    }
    return queue;
}


void p25Phase2ResetSpeakerPendingTimeline(P25Phase2SpeakerPendingQueue& queue) noexcept
{
    queue.callSessionId = 0;
    queue.talkgroupId = 0;
    queue.sourceId = 0;
    queue.grantEpochMs = 0;
    queue.slot = 0xffu;
    queue.frequencyHz = 0;
    queue.nextSpeechOrdinalKnown = false;
    queue.nextSpeechOrdinal = 0;
}


void p25Phase2ClearSpeakerPendingSamplesAndTimeline(P25Phase2SpeakerPendingQueue& queue) noexcept
{
    queue.samples.clear();
    p25Phase2ResetSpeakerPendingTimeline(queue);
}


void p25Phase2BindSpeakerPendingToCall(P25Phase2SpeakerPendingQueue& queue,
                                              const Receiver& rx)
{
    if (rx.p25CurrentCallSessionId == 0) return;
    const double targetFreqHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
    const int64_t roundedTargetHz = p25RoundFrequencyHz(targetFreqHz);
    const uint8_t currentSlot = rx.p25VoiceTdmaSlotKnown
        ? static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u)
        : 0xffu;
    const bool identityChanged =
        (queue.callSessionId != 0 && queue.callSessionId != rx.p25CurrentCallSessionId) ||
        (queue.talkgroupId != 0 && rx.p25VoiceTalkgroupId != 0 &&
         queue.talkgroupId != rx.p25VoiceTalkgroupId) ||
        (queue.grantEpochMs != 0 && rx.p25VoiceGrantEpochMs != 0 &&
         queue.grantEpochMs != rx.p25VoiceGrantEpochMs) ||
        (queue.slot < 2 && currentSlot < 2 && queue.slot != currentSlot) ||
        (queue.frequencyHz != 0 && roundedTargetHz != 0 &&
         std::llabs(queue.frequencyHz - roundedTargetHz) > 50);
    if (identityChanged) {
        p25Phase2ClearSpeakerPendingSamplesAndTimeline(queue);
    }
    queue.callSessionId = rx.p25CurrentCallSessionId;
    if (rx.p25VoiceTalkgroupId != 0 || queue.talkgroupId == 0) {
        queue.talkgroupId = rx.p25VoiceTalkgroupId;
    }
    if (queue.sourceId == 0 && rx.p25VoiceSourceId != 0) {
        queue.sourceId = rx.p25VoiceSourceId;
    }
    if (rx.p25VoiceGrantEpochMs != 0 || queue.grantEpochMs == 0) {
        queue.grantEpochMs = rx.p25VoiceGrantEpochMs;
    }
    if (currentSlot < 2 || queue.slot >= 2) {
        queue.slot = currentSlot;
    }
    if (roundedTargetHz != 0 || queue.frequencyHz == 0) {
        queue.frequencyHz = roundedTargetHz;
    }
}


void p25Phase2EndCallSpeakerTimeline(Receiver& rx,
                                            P25Phase2SpeakerPendingQueue& queue,
                                            AudioEngine* engine,
                                            P25PendingClearReason reason)
{
    if (!p25Phase2SpeakerPlaybackClearAllowed(reason)) return;
    if (!queue.samples.empty()) {
        queue.samples.clear();
        ++rx.p25DiagSpeakerPlaybackCleared;
    }
    p25Phase2ResetSpeakerPendingTimeline(queue);
    if (engine) {
        engine->clearBuffers();
    }
}


void p25Phase2ClearSpeakerPendingQueue(Receiver& rx,
                                              P25Phase2SpeakerPendingQueue& queue,
                                              P25PendingClearReason reason)
{
    if (!p25Phase2SpeakerPlaybackClearAllowed(reason)) return;
    if (!queue.samples.empty()) {
        queue.samples.clear();
        ++rx.p25DiagSpeakerPlaybackCleared;
    }
    if (reason == P25PendingClearReason::CallIdentityChanged ||
        reason == P25PendingClearReason::CallEnded ||
        reason == P25PendingClearReason::RetuneOrGeneration ||
        reason == P25PendingClearReason::UserStop) {
        p25Phase2ResetSpeakerPendingTimeline(queue);
    }
}


void p25Phase2ClearSpeakerPlaybackQueue(Receiver& rx,
                                               P25Phase2SpeakerPendingQueue& pendingSpeaker,
                                               P25PendingClearReason reason)
{
    p25Phase2ClearSpeakerPendingQueue(rx, pendingSpeaker, reason);
}


void p25Phase2ClearStaleResultSpeakerPending(P25SpeakerPendingMap& map,
                                                    const ReceiverSessionKey& sessionKey,
                                                    uint64_t callSessionId,
                                                    Receiver& rx,
                                                    P25PendingClearReason reason)
{
    auto it = map.find(sessionKey);
    if (it == map.end()) return;
    if (callSessionId != 0 && it->second.callSessionId != 0 &&
        it->second.callSessionId != callSessionId) {
        return;
    }
    p25Phase2ClearSpeakerPendingQueue(rx, it->second, reason);
    map.erase(it);
}


bool p25Phase2GrantedSlotIsImmutable(const Receiver& rx) noexcept
{
    return rx.p25VoiceDecodeEnabled &&
           rx.p25VoicePhase2 &&
           rx.p25VoiceTdmaSlotKnown &&
           rx.p25CurrentCallSessionId != 0 &&
           rx.p25Phase2GrantedSlotImmutable;
}


void p25Phase2MarkGrantedSlotImmutable(Receiver& rx) noexcept
{
    if (!rx.p25VoicePhase2 || !rx.p25VoiceTdmaSlotKnown || rx.p25CurrentCallSessionId == 0) {
        return;
    }
    rx.p25Phase2GrantedSlotImmutable = true;
    // Cancel any queued destructive probe once the grant slot is locked.
    if (rx.p25VoiceSlotProbePending) {
        rx.p25VoiceSlotProbePending = false;
        rx.p25VoiceSlotProbeRequested = 0;
        ++rx.p25DiagSlotProbeBlocked;
    }
    rx.p25Phase2StickySlotLabelInvert = false;
}


void p25ClearPhase2PendingAudioOnly(Receiver& rx, P25PendingClearReason reason)
{
    const bool hadPending =
        rx.p25SessionState.pendingAudio.armed ||
        !rx.p25SessionState.pendingAudio.ambeFrames.empty() ||
        rx.p25SessionState.pendingAudioOpposite.armed ||
        !rx.p25SessionState.pendingAudioOpposite.ambeFrames.empty() ||
        rx.p25Phase2PendingAudioArmed ||
        !rx.p25Phase2PendingAudio.empty();
    rx.p25SessionState.pendingAudio = {};
    rx.p25SessionState.pendingAudioOpposite = {};
    // Keep legacy receiver-owned fields cleared as well; older diagnostics/tools
    // still look at these names even though the authoritative queue is call-keyed.
    rx.p25Phase2PendingAudio.clear();
    rx.p25Phase2PendingTalkgroupId = 0;
    rx.p25Phase2PendingAudioArmed = false;
    if (hadPending) {
        ++rx.p25DiagPendingAudioCleared;
        if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
            spdlog::info("P25 PENDING_AUDIO_CLEARED reason={} tg={} slot={} session={}",
                         p25PendingClearReasonName(reason),
                         rx.p25VoiceTalkgroupId,
                         static_cast<unsigned>(rx.p25VoiceTdmaSlot & 0x01u),
                         static_cast<unsigned long long>(rx.p25CurrentCallSessionId));
        }
    }
}


void p25ClearPhase2PendingAudio(Receiver& rx)
{
    // Default callers are call-boundary / encrypted / follow-stop paths.
    p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::CallIdentityChanged);
    p25ClearPhase2RecentSecurityEvidence(rx);
}


void p25NotePhase2SecurityLatchChange(Receiver& rx,
                                             P25CallSecurityLatch next,
                                             const char* why)
{
    const auto prev = rx.p25SessionState.callSecurityLatch;
    if (prev == next) return;
    // Monotonic: never Unknown <- Clear/Encrypted until call reset.
    if (prev != P25CallSecurityLatch::Unknown && next == P25CallSecurityLatch::Unknown) {
        return;
    }
    // Never Clear <-> Encrypted without a call reset (encrypted wins if both appear).
    if (prev == P25CallSecurityLatch::Clear && next == P25CallSecurityLatch::Encrypted) {
        rx.p25SessionState.callSecurityLatch = P25CallSecurityLatch::Encrypted;
        ++rx.p25DiagSecurityChanged;
        rx.p25VoiceClearKnown = false;
        rx.p25VoiceEncrypted = true;
        if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
            spdlog::warn("P25 SECURITY_CHANGED Clear->Encrypted why={} tg={}",
                         why ? why : "?", rx.p25VoiceTalkgroupId);
        }
        return;
    }
    if (prev == P25CallSecurityLatch::Encrypted && next == P25CallSecurityLatch::Clear) {
        // Encrypted is fail-closed for the call; ignore clear claims.
        return;
    }
    rx.p25SessionState.callSecurityLatch = next;
    ++rx.p25DiagSecurityChanged;
    if (next == P25CallSecurityLatch::Clear) {
        rx.p25VoiceClearKnown = true;
        rx.p25VoiceEncrypted = false;
        p25Phase2MarkGrantedSlotImmutable(rx);
    } else if (next == P25CallSecurityLatch::Encrypted) {
        rx.p25VoiceClearKnown = false;
        rx.p25VoiceEncrypted = true;
        p25Phase2MarkGrantedSlotImmutable(rx);
    }
    if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
        spdlog::info("P25 SECURITY_CHANGED {}->{} why={} tg={}",
                     static_cast<int>(prev),
                     static_cast<int>(next),
                     why ? why : "?",
                     rx.p25VoiceTalkgroupId);
    }
}


void p25NotePhase2VocoderReset(Receiver& rx, const char* why)
{
    // First-frame / call-boundary resets are expected.  Only count continuity
    // failures after the call has latched security or already emitted audio.
    const bool midCall =
        rx.p25SessionState.callSecurityLatch != P25CallSecurityLatch::Unknown ||
        rx.p25SessionState.sustain.hadSuccessfulEmit ||
        rx.p25SessionState.frameSequencer.acceptedFrames > 0;
    if (midCall) {
        ++rx.p25DiagVocoderReset;
        if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
            spdlog::warn("P25 VOCODER_RESET why={} tg={} slot={} session={}",
                         why ? why : "?",
                         rx.p25VoiceTalkgroupId,
                         static_cast<unsigned>(rx.p25VoiceTdmaSlot & 0x01u),
                         static_cast<unsigned long long>(rx.p25CurrentCallSessionId));
        }
    }
}


void p25Phase2ResetFrameSequencer(Receiver& rx) noexcept
{
    rx.p25SessionState.frameSequencer = {};
}


Phase2VoiceFrameKey p25Phase2VoiceFrameKeyFromBurst(const P25Phase2Burst& burst,
                                                           const P25Phase2VoiceCodeword& cw,
                                                           uint8_t grantSlot)
{
    Phase2VoiceFrameKey key;
    key.slot = grantSlot;
    key.voiceIndex = cw.voiceIndex;
    key.sessionCodewordIdKnown = cw.sessionCodewordIdKnown;
    key.sessionCodewordId = cw.sessionCodewordId;
    key.streamDibitKnown = cw.streamDibitKnown;
    key.streamDibit = cw.streamDibit;
    key.streamBurstStartDibitKnown = cw.streamBurstStartDibitKnown;
    key.streamBurstStartDibit = cw.streamBurstStartDibit;
    key.sessionBurstIdKnown = cw.sessionBurstIdKnown;
    key.sessionBurstId = cw.sessionBurstId;
    if (burst.superframeLocked) {
        key.superframeAnchor = burst.superframeDibitOffset;
        key.burstIndex = burst.superframeBurstIndex;
    } else {
        key.superframeAnchor = cw.dibitOffset;
        key.burstIndex = 0xffu;
    }
    key.burstVoiceCount = p25Phase2BurstVoiceCount(burst.kind);
    return key;
}


Phase2VoiceFrameKey p25Phase2VoiceFrameKeyFromPending(const P25P2PendingAmbeFrame& pending)
{
    if (pending.frameKeyValid) {
        return pending.frameKey;
    }
    Phase2VoiceFrameKey key;
    key.slot = pending.grantSlotKnown
        ? static_cast<uint8_t>(pending.grantSlot & 0x01u)
        : 0xffu;
    key.voiceIndex = pending.voiceIndex;
    key.superframeAnchor = pending.codewordAbsDibit;
    key.burstIndex = 0xffu;
    return key;
}


void p25Phase2ArmFrameSequencerForCall(Receiver& rx) noexcept
{
    auto& seq = rx.p25SessionState.frameSequencer;
    if (seq.armed &&
        seq.callSessionId == rx.p25CurrentCallSessionId &&
        seq.talkgroupId == rx.p25VoiceTalkgroupId &&
        seq.slot == static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u)) {
        return;
    }
    seq = {};
    seq.armed = true;
    seq.callSessionId = rx.p25CurrentCallSessionId;
    seq.talkgroupId = rx.p25VoiceTalkgroupId;
    seq.slot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
}



size_t p25Phase2InsertSequencerGapSilence(Receiver& rx,
                                                   P25VoiceAudioBlock& out,
                                                   double outputRateHz,
                                                   size_t gapFrames);

static void p25Phase2SequencerCommitAcceptedKey(P25Phase2FrameSequencer& seq,
                                                  const Phase2VoiceFrameKey& key);

static P25Phase2SequencerSpeechInput p25Phase2SequencerInputWithCurrentOrdinal(
    const P25Phase2FrameSequencer& seq,
    P25Phase2SequencerSpeechInput input) noexcept
{
    input.speechOrdinalKnown = true;
    input.speechOrdinal = seq.nextSpeechOrdinal;
    return input;
}


void p25Phase2RecordEmittedSpeechOrdinal(
    P25VoiceAudioBlock& out,
    const P25Phase2SequencerSpeechInput& input)
{
    if (input.speechOrdinalKnown && input.speechOrdinal >= 0) {
        out.phase2EmittedSpeechOrdinals.push_back(input.speechOrdinal);
    }
}


bool p25Phase2SameVoiceBurst(const P25Phase2FrameSequencer& seq,
                                     const Phase2VoiceFrameKey& key) noexcept
{
    if (!seq.haveActiveBurst) return false;
    if (key.streamBurstStartDibitKnown && seq.activeStreamBurstStartKnown) {
        return key.streamBurstStartDibit == seq.activeStreamBurstStartDibit &&
               key.slot == seq.activeBurstSlot;
    }
    if (key.sessionBurstIdKnown && seq.activeSessionBurstIdKnown) {
        return key.sessionBurstId == seq.activeSessionBurstId &&
               key.slot == seq.activeBurstSlot;
    }
    if (key.superframeAnchor != seq.activeBurstAnchor) return false;
    return key.burstIndex == seq.activeBurstIndex;
}


void p25Phase2CloseActiveVoiceBurst(Receiver& rx,
                                           P25Phase2FrameSequencer& seq,
                                           std::vector<P25Phase2SequencerSpeechInput>* decodeQueueOut = nullptr,
                                           P25VoiceAudioBlock* gapSilenceOut = nullptr,
                                           double gapSilenceOutputRateHz = 48000.0) noexcept
{
    if (!seq.haveActiveBurst) return;
    const auto emitOneGap = [&]() {
        ++seq.protocolOrderIssues;
        ++seq.nextSpeechOrdinal;
        if (gapSilenceOut != nullptr &&
            (seq.armed ||
             rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear)) {
            p25Phase2InsertSequencerGapSilence(rx, *gapSilenceOut, gapSilenceOutputRateHz, 1);
        } else {
            ++seq.gapSilenceOrdinals;
            ++rx.p25DiagSequencerGapSilence;
        }
    };
    if (seq.activeBurstVoiceCount > 0) {
        while (seq.expectedVoiceIndex < seq.activeBurstVoiceCount) {
            auto& held = seq.heldFutureFrames[seq.expectedVoiceIndex];
            if (held.has_value() && held->haveAmbe) {
                if (decodeQueueOut != nullptr) {
                    decodeQueueOut->push_back(
                        p25Phase2SequencerInputWithCurrentOrdinal(seq, *held));
                    p25Phase2SequencerCommitAcceptedKey(seq, decodeQueueOut->back().key);
                } else {
                    ++seq.nextSpeechOrdinal;
                }
                held.reset();
                ++seq.reorderReleased;
            } else {
                if (held.has_value()) {
                    held.reset();
                    ++seq.reorderExpired;
                }
                emitOneGap();
            }
            seq.expectedVoiceIndex = static_cast<uint8_t>(seq.expectedVoiceIndex + 1u);
        }
    } else {
        for (uint8_t i = seq.expectedVoiceIndex; i < seq.heldFutureFrames.size(); ++i) {
            auto& held = seq.heldFutureFrames[i];
            if (!held.has_value()) continue;
            if (held->haveAmbe) {
                if (decodeQueueOut != nullptr) {
                    decodeQueueOut->push_back(
                        p25Phase2SequencerInputWithCurrentOrdinal(seq, *held));
                    p25Phase2SequencerCommitAcceptedKey(seq, decodeQueueOut->back().key);
                } else {
                    ++seq.nextSpeechOrdinal;
                }
                ++seq.reorderReleased;
            } else {
                ++seq.reorderExpired;
            }
            held.reset();
        }
    }
    seq.haveActiveBurst = false;
    seq.expectedVoiceIndex = 0;
    seq.activeBurstVoiceCount = 0;
    seq.activeSessionBurstIdKnown = false;
    seq.activeSessionBurstId = 0;
    seq.activeStreamBurstStartKnown = false;
    seq.activeStreamBurstStartDibit = 0;
    seq.activeBurstSlot = 0xffu;
    seq.heldFutureFrames = {};
}


void p25Phase2BeginVoiceBurst(Receiver& rx,
                                     P25Phase2FrameSequencer& seq,
                                     const Phase2VoiceFrameKey& key,
                                     std::vector<P25Phase2SequencerSpeechInput>* decodeQueueOut = nullptr,
                                     P25VoiceAudioBlock* gapSilenceOut = nullptr,
                                     double gapSilenceOutputRateHz = 48000.0) noexcept
{
    p25Phase2CloseActiveVoiceBurst(rx, seq, decodeQueueOut, gapSilenceOut, gapSilenceOutputRateHz);
    seq.haveActiveBurst = true;
    seq.activeBurstAnchor = key.superframeAnchor;
    seq.activeBurstIndex = key.burstIndex;
    seq.activeBurstVoiceCount = key.burstVoiceCount;
    seq.expectedVoiceIndex = 0;
    seq.activeSessionBurstId = key.sessionBurstId;
    seq.activeSessionBurstIdKnown = key.sessionBurstIdKnown;
    seq.activeStreamBurstStartDibit = key.streamBurstStartDibit;
    seq.activeStreamBurstStartKnown = key.streamBurstStartDibitKnown;
    seq.activeBurstSlot = key.slot;
    seq.heldFutureFrames = {};
}


size_t p25Phase2InsertSequencerGapSilence(Receiver& rx,
                                                   P25VoiceAudioBlock& out,
                                                   double outputRateHz,
                                                   size_t gapFrames)
{
    if (gapFrames == 0) return 0;
    const size_t samplesPerFrame = static_cast<size_t>(
        std::max(160.0, 160.0 * (outputRateHz / 8000.0) + 0.5));
    for (size_t i = 0; i < gapFrames; ++i) {
        out.audio.insert(out.audio.end(), samplesPerFrame, 0.0f);
        ++out.phase2EmittedPcmFrames;
        ++out.phase2ConcealmentFrames;
    }
    rx.p25SessionState.frameSequencer.gapSilenceOrdinals += gapFrames;
    rx.p25DiagSequencerGapSilence += gapFrames;
    return gapFrames * samplesPerFrame;
}


FrameOrderResult p25Phase2ClassifySpeechFrameOrder(const P25Phase2FrameSequencer& seq,
                                                            const Phase2VoiceFrameKey& key)
{
    for (const auto& seen : seq.recentKeys) {
        if (seen == key) {
            return FrameOrderResult::Duplicate;
        }
    }
    if (!seq.haveLastAcceptedKey) {
        return FrameOrderResult::Expected;
    }
    const int cmp = p25Phase2CompareVoiceFrameKeys(key, seq.lastAcceptedKey);
    if (cmp == 2) {
        return FrameOrderResult::Unorderable;
    }
    if (cmp == 0) {
        return FrameOrderResult::Duplicate;
    }
    if (cmp < 0) {
        return FrameOrderResult::Late;
    }
    return FrameOrderResult::Expected;
}


void p25Phase2SequencerCommitAcceptedKey(P25Phase2FrameSequencer& seq,
                                                  const Phase2VoiceFrameKey& key)
{
    constexpr size_t kMaxRecentKeys = 256;
    if (seq.recentKeys.size() >= kMaxRecentKeys) {
        seq.recentKeys.erase(seq.recentKeys.begin(),
            seq.recentKeys.begin() + static_cast<std::ptrdiff_t>(seq.recentKeys.size() / 2));
    }
    seq.recentKeys.push_back(key);
    seq.lastAcceptedKey = key;
    seq.haveLastAcceptedKey = true;
    ++seq.acceptedFrames;
    ++seq.nextSpeechOrdinal;
}


void p25Phase2SequencerFlushHeldIntoQueue(P25Phase2FrameSequencer& seq,
                                                 std::vector<P25Phase2SequencerSpeechInput>& decodeQueue)
{
    while (seq.haveActiveBurst &&
           (seq.activeBurstVoiceCount == 0 ||
            seq.expectedVoiceIndex < seq.activeBurstVoiceCount) &&
           seq.expectedVoiceIndex < seq.heldFutureFrames.size()) {
        auto& held = seq.heldFutureFrames[seq.expectedVoiceIndex];
        if (!held.has_value() || !held->haveAmbe) break;
        decodeQueue.push_back(
            p25Phase2SequencerInputWithCurrentOrdinal(seq, *held));
        held.reset();
        ++seq.reorderReleased;
        p25Phase2SequencerCommitAcceptedKey(seq, decodeQueue.back().key);
        seq.expectedVoiceIndex = static_cast<uint8_t>(seq.expectedVoiceIndex + 1u);
    }
    if (seq.haveActiveBurst && seq.activeBurstVoiceCount > 0 &&
        seq.expectedVoiceIndex >= seq.activeBurstVoiceCount) {
        seq.haveActiveBurst = false;
        seq.expectedVoiceIndex = 0;
        seq.heldFutureFrames = {};
    }
}


// Process one speech frame through the Voice2/Voice4 sequencer.  Returns every
// AMBE payload that is now in-order and ready for vocoder decode.
std::vector<P25Phase2SequencerSpeechInput> p25Phase2SequencerProcessSpeechFrame(
    Receiver& rx,
    P25Phase2SequencerSpeechInput incoming,
    P25VoiceAudioBlock* gapSilenceOut = nullptr,
    double gapSilenceOutputRateHz = 48000.0)
{
    std::vector<P25Phase2SequencerSpeechInput> decodeQueue;
    p25Phase2ArmFrameSequencerForCall(rx);
    auto& seq = rx.p25SessionState.frameSequencer;
    const Phase2VoiceFrameKey& key = incoming.key;

    if (!p25Phase2VoiceFrameKeyHasProtocolIdentity(key) ||
        key.slot >= 2 ||
        key.voiceIndex >= 4 ||
        key.burstVoiceCount == 0) {
        ++seq.protocolOrderIssues;
        return decodeQueue;
    }

    const FrameOrderResult order = p25Phase2ClassifySpeechFrameOrder(seq, key);
    if (order == FrameOrderResult::Duplicate) {
        ++seq.duplicateOrLateDrops;
        ++rx.p25DiagSequencerLateDrops;
        return decodeQueue;
    }
    if (order == FrameOrderResult::Unorderable) {
        ++seq.outOfOrderDrops;
        ++rx.p25DiagSequencerOutOfOrderDrops;
        return decodeQueue;
    }
    if (order == FrameOrderResult::Late) {
        ++seq.duplicateOrLateDrops;
        ++seq.outOfOrderDrops;
        ++rx.p25DiagSequencerOutOfOrderDrops;
        ++rx.p25DiagSequencerLateDrops;
        return decodeQueue;
    }

    const bool sameBurst = p25Phase2SameVoiceBurst(seq, key);
    if (!sameBurst) {
        p25Phase2BeginVoiceBurst(rx, seq, key, &decodeQueue, gapSilenceOut, gapSilenceOutputRateHz);
    } else if (key.burstVoiceCount > seq.activeBurstVoiceCount) {
        seq.activeBurstVoiceCount = key.burstVoiceCount;
    }

    if (sameBurst && key.voiceIndex > seq.expectedVoiceIndex) {
        if (incoming.haveAmbe && key.voiceIndex < seq.heldFutureFrames.size()) {
            seq.heldFutureFrames[key.voiceIndex] = incoming;
            ++seq.reorderHeld;
        }
        return decodeQueue;
    }

    if (key.voiceIndex < seq.expectedVoiceIndex) {
        ++seq.protocolOrderIssues;
        ++seq.outOfOrderDrops;
        ++rx.p25DiagSequencerOutOfOrderDrops;
        return decodeQueue;
    }

    if (incoming.haveAmbe) {
        decodeQueue.push_back(
            p25Phase2SequencerInputWithCurrentOrdinal(seq, incoming));
    }
    p25Phase2SequencerCommitAcceptedKey(seq, key);
    seq.expectedVoiceIndex = static_cast<uint8_t>(key.voiceIndex + 1u);
    p25Phase2SequencerFlushHeldIntoQueue(seq, decodeQueue);

    if (seq.haveActiveBurst && seq.activeBurstVoiceCount > 0 &&
        seq.expectedVoiceIndex >= seq.activeBurstVoiceCount) {
        seq.haveActiveBurst = false;
        seq.expectedVoiceIndex = 0;
        seq.heldFutureFrames = {};
    }
    return decodeQueue;
}


void p25Phase2ResetTrafficTargetOffset(Receiver& rx) noexcept
{
    rx.p25Phase2TrafficTargetOffsetKnown = false;
    rx.p25Phase2TrafficTargetOffsetHz = 0.0;
    rx.p25Phase2TrafficTargetOffsetTrust = 0;
    rx.p25Phase2TrafficTargetOffsetMisses = 0;
    if (rx.p25TrafficRetunesPrimary) {
        rx.p25AfcFrozen = false;
        rx.p25FrozenAfcOffsetHz = 0.0;
    }
}


double p25Phase2EffectiveTrafficTargetOffsetHz(const Receiver& rx) noexcept
{
    if (!rx.p25Phase2TrafficTargetOffsetKnown || !std::isfinite(rx.p25Phase2TrafficTargetOffsetHz)) {
        return 0.0;
    }
    const double offsetHz = rx.p25Phase2TrafficTargetOffsetHz;
    const double maxHz = rx.p25TrafficRetunesPrimary
        ? kP25Phase2TrafficTargetOffsetMaxHz
        : 45000.0;
    if (std::abs(offsetHz) < 50.0 || std::abs(offsetHz) > maxHz) {
        return 0.0;
    }
    return offsetHz;
}


double p25Phase2VoiceSchedulerNominalHz(const Receiver& rx) noexcept
{
    if (rx.p25IndependentTrafficSource &&
        rx.p25TrafficVoiceFreqHz > 0.0 && std::isfinite(rx.p25TrafficVoiceFreqHz)) {
        return rx.p25TrafficVoiceFreqHz;
    }
    return rx.freqHz;
}


double p25Phase2TrafficSourceCenterHz(const Receiver& rx) noexcept
{
    if (rx.p25IndependentTrafficSource &&
        rx.p25TrafficSourceCenterFreqHz > 0.0 &&
        std::isfinite(rx.p25TrafficSourceCenterFreqHz)) {
        return rx.p25TrafficSourceCenterFreqHz;
    }
    if (rx.p25IndependentTrafficSource &&
        rx.p25TrafficRetunesPrimary &&
        rx.p25TrafficVoiceFreqHz > 0.0 &&
        std::isfinite(rx.p25TrafficVoiceFreqHz)) {
        return rx.p25TrafficVoiceFreqHz;
    }
    return rx.freqHz;
}


double p25TranscriptVoiceLabelHz(const Receiver& rx,
                                        double decoderTargetHz,
                                        double fallbackVoiceHz) noexcept
{
    if (rx.p25TrafficVoiceFreqHz > 0.0 && std::isfinite(rx.p25TrafficVoiceFreqHz)) {
        return rx.p25TrafficVoiceFreqHz;
    }
    if (fallbackVoiceHz > 0.0 && std::isfinite(fallbackVoiceHz)) {
        return fallbackVoiceHz;
    }
    if (decoderTargetHz > 0.0 && std::isfinite(decoderTargetHz)) {
        return decoderTargetHz;
    }
    if (rx.freqHz > 0.0 && std::isfinite(rx.freqHz)) {
        return rx.freqHz;
    }
    return 0.0;
}


bool p25Phase2PendingAudioMatches(Receiver& rx, const P25P2CallAudioKey& key)
{
    const auto& queue = rx.p25SessionState.pendingAudio;
    return queue.armed && queue.key == key;
}


void p25Phase2HandlePttStartForPendingQueue(Receiver& rx, const P25P2CallAudioKey& audioKey)
{
    if (!audioKey.valid()) return;
    if (p25Phase2PendingAudioMatches(rx, audioKey) &&
        !rx.p25SessionState.pendingAudio.ambeFrames.empty()) {
        return;
    }
    const bool hasAnyPending =
        rx.p25SessionState.pendingAudio.armed ||
        !rx.p25SessionState.pendingAudio.ambeFrames.empty() ||
        rx.p25SessionState.pendingAudioOpposite.armed ||
        !rx.p25SessionState.pendingAudioOpposite.ambeFrames.empty() ||
        rx.p25Phase2PendingAudioArmed ||
        !rx.p25Phase2PendingAudio.empty();
    if (!hasAnyPending) return;
    if (rx.p25SessionState.lastPttStartPendingClearCallSessionId == audioKey.callSessionId) {
        return;
    }
    rx.p25SessionState.lastPttStartPendingClearCallSessionId = audioKey.callSessionId;
    p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::PttStartReset);
}


void p25Phase2ResetVocoderForNewTalkspurt(Receiver& rx, const char* why, qint64 nowMs)
{
    // Keep abs-dibit de-dupe / call security latch / opposite module. Only wipe
    // predictor state that bleeds across talkers on the same grant (DEC-0062 /
    // capture 20260912_225923: one RID clear, later talkers unintelligible).
    p25NotePhase2VocoderReset(rx, why ? why : "talkspurt");
    rx.p25AmbeVoiceDecoder = P25AmbeVoiceDecoder();
    rx.p25SessionState.resampler = {};
    rx.p25SessionState.audioTail = {};
    rx.p25Phase2LastGoodPcm.clear();
    rx.p25SessionState.frameSequencer.resetForTalkspurt();
    rx.p25Phase2PreferredAmbeVariant = -1;
    rx.p25Phase2PreferredAmbeVariantHits = 0;
    rx.p25Phase2PreferredAmbeVariantMisses = 0;
    rx.p25Phase2PreferredAmbeVariantByVoiceIndex.fill(-1);
    rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex.fill(0);
    rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex.fill(0);
    rx.p25Phase2LastTalkspurtVocoderResetMs = nowMs;
    rx.p25Phase2TalkspurtEndedPendingVocoderReset = false;
}


void p25Phase2ObserveTargetTalkspurtMac(Receiver& rx,
                                        const P25Phase2Burst& burst,
                                        bool targetSlot,
                                        qint64 nowMs,
                                        bool positionKnown,
                                        uint64_t absoluteDibit)
{
    const bool boundary = burst.macPttSeen || burst.macEndPttSeen ||
        burst.macIdleSeen || burst.macHangtimeSeen;
    if (!targetSlot || !boundary || !positionKnown ||
        !rx.p25SessionState.talkspurtOrder.acceptBoundary(absoluteDibit)) return;
    const bool spoken =
        rx.p25SessionState.sustain.hadSuccessfulEmit ||
        rx.p25Phase2CallHadSpeakerAudio;
    if (burst.macEndPttSeen || burst.macIdleSeen || burst.macHangtimeSeen) {
        if (spoken) {
            rx.p25Phase2TalkspurtEndedPendingVocoderReset = true;
        }
        return;
    }
    if (!burst.macPttSeen) return;
    if (!spoken) return;
    // Capture position, rather than wall-clock replay speed, identifies a PTT.
    const char* why = rx.p25Phase2TalkspurtEndedPendingVocoderReset
        ? "mac-ptt-after-end"
        : "mac-ptt-talkspurt";
    p25Phase2ResetVocoderForNewTalkspurt(rx, why, nowMs);
}


size_t p25Phase2PendingAmbeFrameCount(Receiver& rx, const P25P2CallAudioKey& key)
{
    const auto& queue = rx.p25SessionState.pendingAudio;
    if (!queue.armed || !(queue.key == key)) return 0;
    return queue.ambeFrames.size();
}


size_t p25Phase2PendingAudioSampleCount(Receiver& rx, const P25P2CallAudioKey& key)
{
    // Diagnostics and old UI fields still speak in post-resample sample counts.
    // A Phase-2 AMBE frame is 20 ms, so at 48 kHz it is approximately 960 samples.
    return p25Phase2PendingAmbeFrameCount(rx, key) * 960u;
}


bool p25QueuePhase2PendingAmbeFrame(Receiver& rx,
                                           const P25P2CallAudioKey& key,
                                           const P25P2PendingAmbeFrame& frame)
{
    if (!key.valid()) return false;
    auto& queue = rx.p25SessionState.pendingAudio;
    if (!queue.armed || !(queue.key == key)) {
        queue = P25P2PendingAudioQueue{};
        queue.key = key;
        queue.armed = true;
    }
    if (frame.haveAbsoluteDibits) {
        // AMBE start offsets for adjacent Phase-2 voice codewords are roughly
        // 36-37 dibits apart.  A 40-dibit duplicate window suppresses valid
        // neighbouring AMBE frames and produces the classic "every other frame"
        // choppy audio.  Keep this tolerance tight: it is only for overlapping
        // decode-window jitter of the same recovered codeword.
        constexpr uint64_t kPendingDuplicateToleranceDibits = 12u;
        for (const auto& existing : queue.ambeFrames) {
            if (!existing.haveAbsoluteDibits) continue;
            const uint64_t delta = frame.codewordAbsDibit > existing.codewordAbsDibit
                ? frame.codewordAbsDibit - existing.codewordAbsDibit
                : existing.codewordAbsDibit - frame.codewordAbsDibit;
            if (delta <= kPendingDuplicateToleranceDibits) {
                return false;
            }
        }
    }
    queue.ambeFrames.push_back(frame);
    while (queue.ambeFrames.size() > kP25Phase2PendingQueueMaxFrames) {
        queue.ambeFrames.pop_front();
    }

    // Mirror the authoritative queue into the legacy receiver fields for existing
    // diagnostics only.  Release decisions use the strongly keyed AMBE map above.
    // Compatibility marker: this is the raw-frame replacement for the old
    // p25QueuePhase2PendingAudio PCM queue.
    rx.p25Phase2PendingAudio.assign(queue.ambeFrames.size() * 960u, 0.0f);
    rx.p25Phase2PendingTalkgroupId = key.talkgroupId;
    rx.p25Phase2PendingAudioArmed = true;
    return true;
}


std::vector<P25P2PendingAmbeFrame> p25TakePhase2PendingAmbeFrames(
    Receiver& rx,
    const P25P2CallAudioKey& key,
    size_t maxFrames = std::numeric_limits<size_t>::max())
{
    std::vector<P25P2PendingAmbeFrame> frames;
    auto& queue = rx.p25SessionState.pendingAudio;
    if (!(queue.armed && queue.key == key) || queue.ambeFrames.empty() || maxFrames == 0) {
        return frames;
    }

    std::vector<P25P2PendingAmbeFrame> sorted(queue.ambeFrames.begin(), queue.ambeFrames.end());
    auto pendingFrameRank = [](const P25P2PendingAmbeFrame& frame) -> int {
        if (!frame.frameKeyValid) return 2;
        if (frame.frameKey.streamDibitKnown && frame.frameKey.sessionBurstIdKnown) return 0;
        if (frame.frameKey.streamDibitKnown || frame.frameKey.sessionBurstIdKnown) return 1;
        return 2;
    };
    std::sort(sorted.begin(), sorted.end(), [&](const P25P2PendingAmbeFrame& a,
                                               const P25P2PendingAmbeFrame& b) {
        // SDRTrunk feeds queued voice timeslots in stream arrival order.  Rank
        // helps only as a tie-breaker; making it primary can move a later
        // fully-keyed AMBE frame ahead of an earlier absolute-positioned frame,
        // which presents mbelib with a subtly doubled/out-of-order speech stream.
        if (a.frameKeyValid && b.frameKeyValid &&
            a.frameKey.streamDibitKnown && b.frameKey.streamDibitKnown &&
            a.frameKey.streamDibit != b.frameKey.streamDibit) {
            return a.frameKey.streamDibit < b.frameKey.streamDibit;
        }
        if (a.haveAbsoluteDibits && b.haveAbsoluteDibits &&
            a.codewordAbsDibit != b.codewordAbsDibit) {
            return a.codewordAbsDibit < b.codewordAbsDibit;
        }
        const int rankA = pendingFrameRank(a);
        const int rankB = pendingFrameRank(b);
        if (rankA != rankB) return rankA < rankB;
        if (rankA == 0 && a.frameKeyValid && b.frameKeyValid) {
            const int cmp = p25Phase2CompareVoiceFrameKeys(a.frameKey, b.frameKey);
            if (cmp != 2) return cmp < 0;
        }
        if (a.haveAbsoluteDibits != b.haveAbsoluteDibits) return a.haveAbsoluteDibits;
        if (a.codewordAbsDibit != b.codewordAbsDibit) return a.codewordAbsDibit < b.codewordAbsDibit;
        return a.voiceIndex < b.voiceIndex;
    });

    const size_t takeCount = std::min(maxFrames, sorted.size());
    frames.assign(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(takeCount));
    if (takeCount >= sorted.size()) {
        queue = {};
        rx.p25Phase2PendingAudio.clear();
        rx.p25Phase2PendingTalkgroupId = 0;
        rx.p25Phase2PendingAudioArmed = false;
    } else {
        queue.ambeFrames.assign(sorted.begin() + static_cast<std::ptrdiff_t>(takeCount),
                                sorted.end());
        queue.armed = true;
        queue.key = key;
        rx.p25Phase2PendingAudio.assign(queue.ambeFrames.size() * 960u, 0.0f);
        rx.p25Phase2PendingTalkgroupId = key.talkgroupId;
        rx.p25Phase2PendingAudioArmed = true;
    }
    return frames;
}


std::vector<P25P2PendingAmbeFrame> p25TakePhase2PendingAudio(
    Receiver& rx,
    const P25P2CallAudioKey& key,
    size_t maxFrames = std::numeric_limits<size_t>::max())
{
    // Compatibility name for the sdrtrunk-style "queued audio timeslot" stage.
    // The queue stores raw AMBE frames so release can run through the same vocoder,
    // validation, and absolute-dibit de-dupe path as fresh voice.
    return p25TakePhase2PendingAmbeFrames(rx, key, maxFrames);
}



constexpr double kP25DecodedAudioSafeMaxPeak = 64.00;
constexpr double kP25DecodedAudioSafeMaxRms = 32.00;

bool p25AudioSamplesLookSafe(const std::vector<float>& audio) noexcept
{
    if (audio.empty()) return false;
    double peak = 0.0;
    double sum2 = 0.0;
    size_t count = 0;
    for (float sample : audio) {
        if (!std::isfinite(sample)) return false;
        const double v = static_cast<double>(sample);
        peak = std::max(peak, std::abs(v));
        sum2 += v * v;
        ++count;
    }
    if (count == 0) return false;
    const double rms = std::sqrt(sum2 / static_cast<double>(count));
    // This is a corruption guard only, not a speech/silence/gain gate.
    // Valid Phase-2 AMBE concealment and pauses can be near-zero, and the old
    // low-RMS/high-RMS gate muted finite decoded frames, starving the audio ring.
    return !(peak > kP25DecodedAudioSafeMaxPeak || rms > kP25DecodedAudioSafeMaxRms);
}


bool p25Phase2AmbeVariantInstabilityFatal(const P25VoiceAudioBlock& out) noexcept
{
    if (!out.phase2AmbeVariantUnstable && out.phase2AmbeVariantChanges == 0) return false;
    const bool targetVoicePresent = out.phase2TargetVoiceCodewords > 0 ||
        (out.phase2VoiceCodewords > 0 && out.phase2WrongSlotVoiceCodewords == 0);
    const bool strongMaskedAudio = out.phase2MaskedBursts >= 6 &&
        targetVoicePresent &&
        out.phase2AmbeAcceptedFrames >= 2 &&
        out.decodedFrames >= 2 &&
        p25AudioSamplesLookSafe(out.audio);
    // Treat variant churn as diagnostic, not fatal, once the current followed
    // slot already has target-slot, masked, finite AMBE PCM.  sdrtrunk queues
    // voice timeslots first and lets PTT/ESS settle security; requiring security
    // evidence before queuing caused late-entry clear audio to self-mute forever.
    return !strongMaskedAudio;
}


bool p25Phase2StrongVoiceTimeslotPcm(const P25VoiceAudioBlock& out) noexcept
{
    const bool targetVoicePresent = out.phase2TargetVoiceCodewords > 0 ||
        (out.phase2VoiceCodewords > 0 && out.phase2WrongSlotVoiceCodewords == 0 && !out.phase2WrongSlot);
    // Do not promote late-entry/probe fragments or single AMBE blips as strong
    // clear voice.  Require the same bounded diagnostic-probe minimum before
    // any future fallback can treat decoded PCM as usable evidence.
    const bool enoughProbePcm =
        out.decodedFrames >= kP25Phase2UnknownGrantAudioProbeMinFrames &&
        out.audio.size() >= kP25Phase2UnknownGrantAudioProbeMinSamples;
    return out.phase2SuperframeBursts >= 6 &&
        out.phase2MaskedBursts >= 6 &&
        targetVoicePresent &&
        enoughProbePcm &&
        p25AudioSamplesLookSafe(out.audio);
}


bool p25Phase2BootstrappedMaskTargetVoiceEvidence(const P25VoiceAudioBlock& out) noexcept
{
    return out.phase2MaskedBursts >= 1 &&
        out.phase2TargetVoiceCodewords >= 2 &&
        (out.phase2SuperframeBursts >= 1 ||
         out.phase2TargetMaskedBursts >= 1 ||
         out.phase2Bursts >= 1);
}


bool p25Phase2StrongLateEntryTargetVoiceEvidence(const Receiver& rx,
                                                        const P25VoiceAudioBlock& out) noexcept
{
    const bool targetVoicePresent = out.phase2TargetVoiceCodewords > 0 ||
        (out.phase2VoiceCodewords > 0 && out.phase2WrongSlotVoiceCodewords == 0 && !out.phase2WrongSlot);
    const bool enoughTargetVoice =
        out.phase2TargetVoiceCodewords >= kP25Phase2LateEntryStrongTargetVoiceCodewords ||
        (out.phase2TargetVoiceCodewords == 0 &&
         out.phase2VoiceCodewords >= kP25Phase2LateEntryStrongTargetVoiceCodewords &&
         out.phase2WrongSlotVoiceCodewords == 0 &&
         !out.phase2WrongSlot);
    const bool enoughTargetMask =
        out.phase2TargetMaskedBursts >= kP25Phase2LateEntryStrongTargetMaskedBursts ||
        (out.phase2TargetVoiceCodewords > 0 &&
         out.phase2MaskedBursts >= kP25Phase2LateEntryStrongMaskedBursts) ||
        p25Phase2BootstrappedMaskTargetVoiceEvidence(out);

    return rx.p25VoicePhase2 &&
        rx.p25VoiceTdmaSlotKnown &&
        rx.p25VoiceMaskParamsKnown &&
        !rx.p25VoiceEncrypted &&
        !out.skippedEncrypted &&
        !out.phase2TargetEssEncrypted &&
        !out.phase2WrongSlot &&
        (out.phase2SuperframeBursts >= kP25Phase2LateEntryStrongSuperframeBursts ||
         p25Phase2BootstrappedMaskTargetVoiceEvidence(out)) &&
        (out.phase2MaskedBursts >= kP25Phase2LateEntryStrongMaskedBursts ||
         p25Phase2BootstrappedMaskTargetVoiceEvidence(out)) &&
        targetVoicePresent &&
        (enoughTargetVoice || p25Phase2BootstrappedMaskTargetVoiceEvidence(out)) &&
        enoughTargetMask;
}


bool p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(const Receiver& rx,
                                                           const P25VoiceAudioBlock& out) noexcept
{
    const bool targetVoicePresent = out.phase2TargetVoiceCodewords > 0 ||
        (out.phase2VoiceCodewords > 0 && out.phase2WrongSlotVoiceCodewords == 0 && !out.phase2WrongSlot);
    return rx.p25VoicePhase2 &&
        rx.p25VoiceTdmaSlotKnown &&
        rx.p25VoiceMaskParamsKnown &&
        !rx.p25VoiceEncrypted &&
        out.phase2TargetEssKnown &&
        !out.phase2TargetEssEncrypted &&
        out.phase2TargetMacCrcValid &&
        out.phase2MacCrcValid > 0 &&
        out.phase2SuperframeBursts >= 6 &&
        out.phase2MaskedBursts >= 6 &&
        targetVoicePresent;
}


bool p25Phase2TargetPttSessionClear(const P25VoiceAudioBlock& out) noexcept
{
    // sessionAudioRelease is emitted by the Phase-2 decoder only for the
    // followed clear traffic session (ESS/PTT or MAC_ACTIVE clear traffic SO).
    // Requiring securityStateFromPtt here drops valid clipped MAC_ACTIVE calls.
    return out.phase2TargetSessionAudioRelease &&
        !out.phase2TargetEssEncrypted;
}


bool p25Phase2TargetEssClear(const P25VoiceAudioBlock& out) noexcept
{
    return out.phase2TargetEssKnown && !out.phase2TargetEssEncrypted;
}


bool p25Phase2TargetHardClearEvidence(const P25VoiceAudioBlock& out) noexcept
{
    return p25Phase2TargetPttSessionClear(out) || p25Phase2TargetEssClear(out);
}


bool p25Phase2DualSlotUntrustedGarbleWindow(const P25VoiceAudioBlock& out) noexcept;
bool p25Phase2PostEmitMixedMacDeadWindow(const Receiver& rx,
                                               const P25VoiceAudioBlock& out) noexcept;

static bool p25Phase2ExplicitClearGrantTargetMacTrafficProof(const Receiver& rx,
                                                             const P25VoiceAudioBlock& out) noexcept
{
    if (!rx.p25VoicePhase2 ||
        !rx.p25VoiceClearKnown ||
        rx.p25VoiceEncrypted ||
        !rx.p25VoiceTdmaSlotKnown ||
        !rx.p25VoiceMaskParamsKnown) {
        return false;
    }
    if (out.skippedEncrypted ||
        out.phase2TargetEssEncrypted ||
        out.phase2WrongSlot ||
        p25Phase2DualSlotUntrustedGarbleWindow(out)) {
        return false;
    }

    // SDRTrunk's traffic channel has already been allocated by the clear grant.
    // If the followed timeslot now has current target MAC lock/CRC plus masked
    // target VCWs, treat that as traffic-channel proof for draining/feeding the
    // selected audio module.  This is deliberately narrower than grant-clear
    // alone, so stale aggregate MAC or AMBE plausibility cannot open the speaker.
    const bool currentTargetMac = out.phase2TargetMacCrcValid;
    const bool selectedTargetVoice =
        out.phase2TargetVoiceCodewords >= kP25Phase2ExplicitClearGrantProbeMinFrames &&
        out.phase2TargetMaskedBursts > 0 &&
        out.phase2MaskedBursts > 0;
    const bool selectedTargetStructure =
        out.phase2SuperframeBursts >= 2 ||
        out.phase2TargetMaskedBursts >= 2;
    return currentTargetMac && selectedTargetVoice && selectedTargetStructure;
}


bool p25Phase2MacEssStarvedVoiceWindow(const P25VoiceAudioBlock& out) noexcept
{
    const bool targetVoiceEvidence =
        out.phase2TargetVoiceCodewords > 0 ||
        out.phase2ExpectedVoiceCodewords > 0 ||
        out.phase2DiagnosticAmbeProbeAttempts > 0;
    return targetVoiceEvidence &&
        out.phase2MaskedBursts > 0 &&
        out.phase2TargetMaskedBursts > 0 &&
        !out.phase2TargetMacCrcValid &&
        !out.phase2TargetEssKnown &&
        out.phase2MacCrcValid == 0 &&
        out.phase2FedToMbelib == 0 &&
        out.decodedFrames == 0 &&
        out.audio.empty();
}


bool p25Phase2WindowHasFreshTargetEvidence(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2ExplicitClearGrantVoiceReleaseEvidence(const Receiver& rx,
                                                            const P25VoiceAudioBlock& out) noexcept
{
    const bool targetTrafficClearEvidence =
        p25Phase2TargetHardClearEvidence(out) ||
        p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out) ||
        p25Phase2ExplicitClearGrantTargetMacTrafficProof(rx, out);

    return rx.p25VoicePhase2 &&
        rx.p25VoiceTdmaSlotKnown &&
        rx.p25VoiceMaskParamsKnown &&
        rx.p25VoiceClearKnown &&
        !rx.p25VoiceEncrypted &&
        !out.skippedEncrypted &&
        !out.phase2TargetEssEncrypted &&
        !out.phase2WrongSlot &&
        // Capture 20260808_034136: explicit-clear must never release speaker
        // through MAC-dead dual-slot (sticky grant-clear != this-window MAC).
        !p25Phase2DualSlotUntrustedGarbleWindow(out) &&
        // Capture 20260908_041716 seq=134: this-window ESS clear made
        // DualSlotUntrusted false, then explicit-clear still released mixed
        // MAC-dead PCM. After the call has spoken, mixed windows need
        // this-window selected MAC CRC (DEC-0012 feed-only; no hop PCM clear).
        !p25Phase2PostEmitMixedMacDeadWindow(rx, out) &&
        // The control-channel grant only selects the traffic timeslot.  Match
        // SDRTrunk: queued AMBE is drained only after the traffic slot proves
        // clear through target PTT/ESS/session state or late-entry proof.
        targetTrafficClearEvidence;
}


bool p25Phase2UnknownGrantProbeVoiceReleaseEvidence(const Receiver& rx,
                                                           const P25VoiceAudioBlock& out) noexcept
{
    (void)rx;
    (void)out;
    // Default-off lab probes may decode AMBE into a throwaway vocoder for
    // diagnostics, but they are never clear-call proof. SDRTrunk releases queued
    // Phase-2 audio only after target-slot PTT/ESS establishes call security.
    return false;
}


bool p25Phase2WindowHasFreshTargetEvidence(const P25VoiceAudioBlock& out) noexcept
{
    return out.phase2TargetVoiceCodewords > 0 || out.phase2TargetMaskedBursts > 0;
}


bool p25Phase2BlockHasTrustedClearContext(const P25VoiceAudioBlock& out) noexcept
{
    if (out.skippedEncrypted || out.phase2TargetEssEncrypted) return false;
    // Opposite-only wrong-slot windows are not trusted. Dual-slot windows that
    // still carry selected-slot voice remain eligible for clear continuity/PLC.
    if (out.phase2WrongSlot && out.phase2TargetVoiceCodewords == 0) return false;
    return out.phase2SecurityTrustedClear ||
        p25Phase2TargetHardClearEvidence(out) ||
        out.phase2SdrtrunkLateEntryVoiceRelease ||
        out.phase2ExplicitClearGrantVoiceRelease;
}


bool p25Phase2TrustedConcealmentOnlyWindow(const P25VoiceAudioBlock& out) noexcept
{
    return out.decodedFrames == 0 &&
        !out.audio.empty() &&
        out.phase2InputQualityRejectedVoiceCodewords > 0 &&
        out.phase2EmittedPcmFrames > 0 &&
        out.phase2ConcealmentFrames < out.phase2EmittedPcmFrames &&
        out.phase2FedToMbelib > 0 &&
        out.phase2TargetVoiceCodewords > 0 &&
        !out.skippedEncrypted &&
        !out.phase2TargetEssEncrypted &&
        !out.phase2WrongSlot &&
        p25Phase2BlockHasTrustedClearContext(out) &&
        p25AudioSamplesLookSafe(out.audio);
}


bool p25VoiceBlockHasSpeakerTimelineAudio(const P25VoiceAudioBlock& out) noexcept
{
    if (out.decodedFrames > 0 && !out.audio.empty()) return true;
    return p25Phase2TrustedConcealmentOnlyWindow(out);
}


bool p25Phase2RollingDecodeWindowConsumed(const P25VoiceAudioBlock& out) noexcept
{
    const bool phase2Path =
        out.phase2Bursts > 0 ||
        out.phase2VoiceCodewords > 0 ||
        out.phase2TargetVoiceCodewords > 0 ||
        out.phase2ExpectedVoiceCodewords > 0 ||
        out.phase2MacPdus > 0 ||
        out.phase2TargetEssKnown ||
        out.phase2EssKnown;
    if (!phase2Path) return true;
    if (out.skippedEncrypted ||
        out.phase2SecurityTrustedEncrypted ||
        out.phase2TargetEssEncrypted) {
        return true;
    }
    if (out.phase2TargetVoiceCodewords == 0 &&
        out.phase2ExpectedVoiceCodewords == 0) {
        return true;
    }
    if (out.phase2TargetVoiceCodewords == 0 &&
        out.phase2FedToMbelib == 0 &&
        out.phase2PendingAmbeFramesQueued == 0) {
        // Expected VCWs are a cadence diagnostic, not recovered selected-slot
        // AMBE. Holding an expected-only/no-target window pinned the rolling
        // cursor after clear calls ended and purged newer near-live jobs.
        return true;
    }
    if (out.phase2PendingAmbeFramesQueued > 0 ||
        (out.phase2PendingAudioSamplesAfter > out.phase2PendingAudioSamplesBefore &&
         out.phase2PendingAudioSamplesAfter > 0)) {
        return true;
    }
    if (out.phase2FedToMbelib > 0 &&
        out.phase2EmittedPcmFrames > 0) {
        // The persistent vocoder/timeline has already advanced. If a later
        // security gate withholds speaker PCM, retrying this same RF cannot
        // recover it because duplicate/order state has also moved forward; it
        // only pins the rolling cursor and purges newer live speech jobs.
        return true;
    }
    const size_t selectedVoiceNeedingDisposition = std::max(
        out.phase2TargetVoiceCodewords,
        out.phase2ExpectedVoiceCodewords);
    const size_t nonSelectedRejectedVoice = std::min(
        out.phase2RejectedVoiceCodewords,
        std::max(out.phase2OppositeVoiceCodewords, out.phase2WrongSlotVoiceCodewords));
    const size_t selectedRejectedVoice =
        out.phase2RejectedVoiceCodewords > nonSelectedRejectedVoice
            ? out.phase2RejectedVoiceCodewords - nonSelectedRejectedVoice
            : 0;
    const size_t selectedQualityRejectedVoice = std::min(
        out.phase2InputQualityRejectedVoiceCodewords,
        selectedVoiceNeedingDisposition);
    const size_t selectedVoiceAlreadyAccounted =
        out.phase2AbsoluteDuplicateSuppressedVoiceCodewords +
        out.phase2SequencerSuppressedVoiceCodewords +
        out.phase2ContextSuppressedVoiceCodewords +
        selectedRejectedVoice +
        selectedQualityRejectedVoice;
    if (selectedVoiceNeedingDisposition > 0 &&
        out.phase2FedToMbelib == 0 &&
        out.phase2PendingAmbeFramesQueued == 0 &&
        selectedVoiceAlreadyAccounted >= selectedVoiceNeedingDisposition) {
        // Holding this window cannot recover audio: every selected-slot VCW was
        // already classified as replay/context/late, rejected, or quality-gated.
        // Advancing the rolling cursor prevents a stale held range from purging
        // newer live speech jobs.
        return true;
    }
    // DEC-0037 / capture 20260909_100909: DEC-0036 always-advance flipped the
    // good clear follow (095846 duty 0.947) into chirpy little emits (max duty
    // 0.40). Restore hold for eyes that can still open with MAC/ESS on retry.
    // Only skip hold for unknown/waiting-clear starts that never queued AMBE
    // (095846 TG 30302) — those cannot recover by re-decoding the same RF.
    if (out.waitingForClearGrant ||
        out.phase2LateEntryWaiting ||
        out.diag == P25VoiceDiagCode::WaitingForClearGrant) {
        return true;
    }
    // Target-slot VCWs were visible but neither queued nor emitted. Holding the
    // cursor lets a later MAC/ESS or tighter framer lock recover this RF instead
    // of converting it into context-only/duplicate speech on the next hop.
    return false;
}


// Dual-slot carriers routinely carry two TGs.  Capture 20260730_095246 showed
// gate=emit windows with oppVcw>0 and this-window p2mac=0 producing noise-like
// PCM (spectral flatness 0.147 / noise-score 0.157) while clean opp=0 +
// strong-MAC spans scored speech-like (0.107 / 0.118).  The noisy emit windows
// on skip≈616804 had opp=12 with target=16–24 (opp < target) and mac=0/5–0/7 —
// so requiring opp>=target missed them.  SDRTrunk binds one AudioModule per
// timeslot and only plays after clear PTT/ESS on that slot; sticky grant-clear
// must not keep feeding mbelib through MAC-dead dual-slot chaos.  Sticky
// targetEss=clear / sticky phase2TargetMacCrcValid (recent-security latch) must
// NOT defeat this gate — only this-window MAC CRC counts.
//
// Capture 20260808_034136 (user: "more voice but blocky/shonky, nothing can be
// made out"): 63/177 emits dual-slot, ALL with p2mac=0/x, ALL ess=clear sticky,
// ALL companion "accounted" (rej>=opp).  Prior gate trusted those via
// companion+sticky-ESS+sf/mask and released ~38s of wrong-epoch AMBE as
// explicit-clear-grant-traffic-clear-release.  Rejecting opposite labels only
// proves we discarded companion VCWs — it does NOT prove XOR mask epoch on the
// selected slot.  Fail closed: dual-slot requires this-window MAC CRC.
//
// Continuity escapes (selectedSlotContinuityProof / feed continuation via
// latch|hadSuccessfulEmit|speakerSustain) re-opened MAC-dead dual-slot after
// one good emit — removed.  Sticky phase2TargetMacCrcValid / sticky ESS
// (recent-security latch) must NOT count as this-window proof.
//
// Capture 20260809_004206: dual-slot windows with this-window target ESS clear
// + companion accounted + strong selected structure were continuous clear
// speech; blanket MAC==0 mute dropped duty 0.155→0.055.  Allow only when
// this-window selected-slot ESS/MAC/session proves clear (burst evidence),
// never sticky overlay alone.
bool p25Phase2CompanionSlotAccounted(const P25VoiceAudioBlock& out) noexcept
{
    const bool companionSlotLabelledByAggregate =
        out.phase2VoiceCodewords > 0 &&
        out.phase2TargetVoiceCodewords + out.phase2OppositeVoiceCodewords >= out.phase2VoiceCodewords;
    return companionSlotLabelledByAggregate ||
        out.phase2RejectedVoiceCodewords >= out.phase2OppositeVoiceCodewords ||
        out.phase2WrongSlotVoiceCodewords >= out.phase2OppositeVoiceCodewords;
}


bool p25Phase2StrongSelectedSlotStructure(const P25VoiceAudioBlock& out) noexcept
{
    return out.phase2TargetVoiceCodewords >= 2 &&
        (out.phase2TargetMaskedBursts > 0 || out.phase2MaskedBursts > 0);
}


bool p25Phase2DualSlotUntrustedGarbleWindow(const P25VoiceAudioBlock& out) noexcept
{
    if (out.phase2OppositeVoiceCodewords == 0) return false;
    // Opposite-slot-only dwell is not a mixed MAC-dead window. Phase 2 gives
    // the companion timeslot every other burst. Voicetest of capture
    // 20260905_105622 (TG 30003 slot 0, skip=97334, HEAD) logged
    // gate=dual-slot-untrusted-garble-drop with targetVcw=0 oppVcw=4–6 and
    // PASS_PARTIAL_AUDIO drop=D duty=0.28 (ISS-0001, DEC-0003). Do not feed
    // opposite VCWs to the speaker vocoder (slot check). Do not brand the hop
    // as garble or it clears this-window PCM and blocks later selected bursts
    // in the same job.
    if (out.phase2TargetVoiceCodewords == 0) return false;
    const bool companionSlotAccounted = p25Phase2CompanionSlotAccounted(out);
    const bool strongSelectedSlot = p25Phase2StrongSelectedSlotStructure(out);
    const bool selectedTimeslotContinuation =
        out.phase2SameCallSelectedTimeslotContinuation &&
        out.phase2CurrentFeedTrustedTargetBurst &&
        companionSlotAccounted &&
        strongSelectedSlot &&
        !out.phase2TargetEssEncrypted &&
        !out.phase2WrongSlot;
    if (selectedTimeslotContinuation) {
        return false;
    }
    const bool thisWindowSelectedClearProof =
        out.phase2ThisWindowTargetMacCrcValid ||
        (out.phase2ThisWindowTargetEssClear &&
         companionSlotAccounted &&
         strongSelectedSlot);
    // Capture 20260811_072556: ThisWindowTargetSessionAudioRelease alone must
    // not authorize dual-slot MAC-dead windows (191 emits, all ptt=no). Keep
    // this-window ESS clear + companion accounted (20260809_004206), but drop
    // session-release-as-epoch for dual-slot trust.
    if (!thisWindowSelectedClearProof && out.phase2MacCrcValid == 0) {
        return true;
    }
    if (!thisWindowSelectedClearProof &&
        !companionSlotAccounted &&
        out.phase2OppositeVoiceCodewords > out.phase2TargetVoiceCodewords) {
        return true;
    }
    if (!strongSelectedSlot) {
        return true;
    }
    if (!companionSlotAccounted &&
        out.phase2OppositeVoiceCodewords > out.phase2TargetVoiceCodewords) {
        return true;
    }
    return false;
}


// Capture 20260908_041716 seq=134: after seq=131 emit (p2mac=5/6 opp=0), the next
// hop was target=6 opp=12 p2mac=0/0 ess=clear and still
// explicit-clear-grant-traffic-clear-release. DualSlotUntrustedGarbleWindow
// treated this-window ESS + companion accounted as 004206 proof. Operator:
// garbled wrong-slot audio. DEC-0012: after the call has spoken, mixed windows
// without this-window selected MAC CRC and with companion louder than
// selected (oppVcw > targetVcw) must not feed unproven bursts. Selected-
// dominant mixed ESS still feeds (105622 99134). Do not
// fold this into DualSlotUntrustedGarbleWindow — that path audio.clear()s the
// hop and dropped 105622 duty to 0.38.
bool p25Phase2PostEmitMixedMacDeadWindow(const Receiver& rx,
                                                const P25VoiceAudioBlock& out) noexcept
{
    if (!rx.p25VoicePhase2) return false;
    if (!rx.p25SessionState.sustain.hadSuccessfulEmit &&
        !rx.p25Phase2CallHadSpeakerAudio) {
        return false;
    }
    if (out.phase2OppositeVoiceCodewords == 0 ||
        out.phase2TargetVoiceCodewords == 0) {
        return false;
    }
    if (out.phase2ThisWindowTargetMacCrcValid) return false;
    // 105622 startMs=99134: target=18 opp=8 slot0Mac=0 slot1Mac=4 ESS clear.
    // Selected-dominant mixed ESS is 004206 continuous speech; muting every
    // mixed MAC-dead hop after emit dropped duty 0.685→0.27. 041716 seq=134
    // was target=6 opp=12 then "wrong TDMA slot" — companion louder, no
    // selected MAC. DualSlotUntrusted already uses opp>target in the
    // unaccounted-companion branch; reuse that comparison, not a new ratio.
    if (out.phase2TargetVoiceCodewords >= out.phase2OppositeVoiceCodewords) {
        return false;
    }
    return true;
}


bool p25Phase2SameCallSelectedTimeslotContinuationSafe(const Receiver& rx,
                                                              const P25VoiceAudioBlock& out,
                                                              const P25P2CallAudioKey& key,
                                                              qint64 nowMs,
                                                              bool requireFedAudio) noexcept
{
    if (!rx.p25VoicePhase2 || !key.valid()) return false;
    if (rx.p25VoiceEncrypted ||
        out.skippedEncrypted ||
        out.phase2TargetEssEncrypted ||
        out.phase2WrongSlot ||
        out.phase2FeedOrderIssues > 0) {
        return false;
    }
    if (out.phase2OppositeVoiceCodewords == 0 || out.phase2TargetVoiceCodewords == 0) return false;
    if (!p25Phase2CompanionSlotAccounted(out) ||
        !p25Phase2StrongSelectedSlotStructure(out)) {
        return false;
    }
    const bool thisWindowSelectedSlotProof =
        out.phase2ThisWindowTargetMacCrcValid ||
        out.phase2ThisWindowTargetEssClear;
    const bool sameCallClear =
        p25Phase2RecentSecurityEvidenceUsable(rx, key, nowMs) &&
        (rx.p25Phase2RecentTargetMacCrcValid ||
         rx.p25Phase2RecentTargetSessionAudioRelease ||
         (rx.p25Phase2RecentTargetEssKnown && !rx.p25Phase2RecentTargetEssEncrypted)) &&
        (rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear ||
         (rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted));
    if (!sameCallClear) return false;
    const bool recentSelectedSlotProof =
        (rx.p25Phase2RecentTargetMacCrcValid ||
         rx.p25Phase2RecentTargetSessionAudioRelease ||
         (rx.p25Phase2RecentTargetEssKnown &&
          !rx.p25Phase2RecentTargetEssEncrypted)) &&
        (rx.p25Phase2RecentSuperframeMaskLock ||
         out.phase2TargetMaskedBursts > 0 ||
         out.phase2MaskedBursts > 0);
    if (!thisWindowSelectedSlotProof && !recentSelectedSlotProof) {
        return false;
    }
    if (!requireFedAudio) return true;
    if (out.phase2PendingAmbeFramesReleased > 0) return false;
    if (out.phase2FedToMbelib == 0 ||
        out.phase2EmittedPcmFrames == 0 ||
        out.audio.empty() ||
        !p25AudioSamplesLookSafe(out.audio)) {
        return false;
    }
    // Continuation audio is safe only when the live selected slot was the source
    // and the companion slot was rejected/accounted for, never when pending
    // late-entry AMBE was merged into a dual-slot window.
    return out.phase2CurrentFeedTrustedTargetBurst &&
        out.phase2FedToMbelib <= out.phase2TargetVoiceCodewords + out.phase2ConcealmentFrames;
}


bool p25Phase2PostEmitSelectedSlotContinuationSafe(const Receiver& rx,
                                                   const P25VoiceAudioBlock& out,
                                                   const P25P2CallAudioKey& key,
                                                   qint64 nowMs,
                                                   bool requireFedAudio) noexcept
{
    if (!p25Phase2PostEmitMixedMacDeadWindow(rx, out)) return false;
    if (!p25Phase2SameCallSelectedTimeslotContinuationSafe(rx, out, key, nowMs, requireFedAudio)) {
        return false;
    }
    if (!out.phase2CurrentFeedTrustedTargetBurst ||
        out.phase2PendingAmbeFramesReleased > 0 ||
        out.phase2TargetEssEncrypted ||
        out.phase2WrongSlot ||
        out.phase2FeedOrderIssues > 0 ||
        out.skippedEncrypted) {
        return false;
    }

    const bool callAlreadyClear =
        rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear ||
        rx.p25SessionState.sustain.hadSuccessfulEmit ||
        rx.p25Phase2CallHadSpeakerAudio ||
        (rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted);
    if (!callAlreadyClear) return false;

    const bool selectedStructureStillLocked =
        out.phase2TargetMaskedBursts > 0 ||
        out.phase2MaskedBursts > 0 ||
        out.phase2SuperframeBursts > 0 ||
        out.phase2ThisWindowTargetEssClear ||
        out.phase2TargetEssKnown;
    if (!selectedStructureStillLocked) return false;

    // SDRTrunk keeps a dedicated P25P2AudioModule per selected timeslot.  A
    // rolling IQ window can legitimately report a louder companion slot; once
    // the current followed burst is explicitly labelled/trusted, the companion
    // diagnostics must not starve that selected-slot module.
    return p25Phase2CompanionSlotAccounted(out) &&
        p25Phase2StrongSelectedSlotStructure(out) &&
        p25Phase2WindowHasFreshTargetEvidence(out);
}


bool p25Phase2DualSlotPendingDrainUnsafeWindow(const P25VoiceAudioBlock& out) noexcept
{
    // Match speaker/feed: pending drain must not release MAC-dead dual-slot
    // just because sticky ESS/session/explicit-clear is latched.
    return p25Phase2DualSlotUntrustedGarbleWindow(out);
}


bool p25Phase2CurrentSelectedBurstFeedTrusted(
    const P25Phase2Burst& burst,
    std::optional<bool> encryptedForCall = std::nullopt) noexcept
{
    const bool encrypted = encryptedForCall.value_or(burst.encrypted);
    if (!burst.xorMaskApplied || encrypted || burst.voiceCodewords.empty()) return false;
    // sdrtrunk always owns a timeslot label on the traffic audio module.  An
    // unlabelled burst must never feed the live vocoder — that is the dual-call
    // mix path (field multi-talker with oppVcw=0 when epoch mislabels).
    if (!burst.grantSlotKnown) return false;
    // Selected-slot feed trust is per burst. Capture 20260811_072556 showed
    // session/PTT release is security continuity, not XOR/mask epoch proof; let
    // it authorize clear state later, but never use it to prove this burst's
    // descramble epoch.
    const bool currentSecurityLock =
        burst.macCrcValid ||
        burst.macCrcLock;
    if (burst.maskPhaseLock || currentSecurityLock) return true;
    // OP25/sdrtrunk do not require a MAC CRC on every voice-only TDMA burst.
    // Once the selected timeslot is labelled and the burst is superframe-locked
    // with the XOR mask applied, the per-timeslot voice module keeps feeding
    // the vocoder. Requiring the stronger maskPhaseLock here starved clear
    // calls into word-sized islands whenever traffic carried voice-only bursts.
    if (burst.superframeLock && burst.grantSlotKnown) return true;
    if (burst.xorMaskPhaseKnown && burst.superframeLock) return true;
    return false;
}


bool p25Phase2UnsafeMixedSlotAudioWindow(const P25VoiceAudioBlock& out) noexcept
{
    if (out.phase2OppositeVoiceCodewords == 0) return false;
    if (out.phase2WrongSlot) return true;
    if (out.phase2TargetVoiceCodewords == 0) return true;
    if (out.phase2FeedOrderIssues > 0) {
        return true;
    }
    if (out.phase2SameCallSelectedTimeslotContinuation &&
        out.phase2CurrentFeedTrustedTargetBurst &&
        out.phase2FedToMbelib > 0 &&
        out.phase2EmittedPcmFrames > 0 &&
        out.phase2PendingAmbeFramesReleased == 0 &&
        !out.phase2TargetEssEncrypted &&
        !out.skippedEncrypted &&
        p25Phase2CompanionSlotAccounted(out)) {
        return false;
    }
    if (p25Phase2DualSlotUntrustedGarbleWindow(out)) {
        return true;
    }
    // SDRTrunk routes a single selected timeslot into each P25P2AudioModule.
    // A rolling IQ window can still report the companion slot as diagnostics.
    // Once the code above has rejected known-opposite-slot VCWs before mbelib,
    // the companion slot must not mute already-decoded target-slot PCM simply
    // because it is busier than our selected talkgroup.
    return out.phase2FedToMbelib == 0 || out.phase2EmittedPcmFrames == 0;
}


bool p25Phase2CleanPlayoutBridgeAnchorWindow(const P25VoiceAudioBlock& out) noexcept
{
    const bool phase2Path =
        out.phase2Bursts > 0 ||
        out.phase2VoiceCodewords > 0 ||
        out.phase2TargetVoiceCodewords > 0 ||
        out.phase2EmittedPcmFrames > 0;
    if (!phase2Path) return true;
    // SDRTrunk hears the companion timeslot on the same RF; it just never
    // routes it into this audio module.  Seeing oppVcw is normal.  Arm the
    // clock-silence bridge from selected-slot speech as long as the companion
    // was accounted/rejected and no wrong-slot VCW reached mbelib.
    return !p25Phase2UnsafeMixedSlotAudioWindow(out) &&
        out.phase2WrongSlotVoiceCodewords == 0 &&
        !out.phase2WrongSlot &&
        p25Phase2CompanionSlotAccounted(out) &&
        out.phase2TargetVoiceCodewords > 0 &&
        out.phase2FedToMbelib > 0 &&
        out.phase2EmittedPcmFrames > 0;
}


bool p25Phase2WindowDisablesPlayoutBridge(const P25VoiceAudioBlock& out) noexcept
{
    const bool phase2Path =
        out.phase2Bursts > 0 ||
        out.phase2VoiceCodewords > 0 ||
        out.phase2TargetVoiceCodewords > 0 ||
        out.phase2OppositeVoiceCodewords > 0 ||
        out.phase2EmittedPcmFrames > 0;
    if (!phase2Path) return false;
    return p25Phase2UnsafeMixedSlotAudioWindow(out) ||
        out.phase2WrongSlotVoiceCodewords > 0 ||
        out.phase2WrongSlot ||
        (out.phase2TargetVoiceCodewords > 0 && out.phase2FedToMbelib == 0);
}


bool p25Phase2AudioTailGraceActive(const Receiver& rx) noexcept;

bool p25Phase2SpeakerOutputCanRefreshFollowActivity(const P25VoiceAudioBlock& out) noexcept
{
    const bool phase2Path =
        out.phase2Bursts > 0 ||
        out.phase2VoiceCodewords > 0 ||
        out.phase2EmittedPcmFrames > 0 ||
        out.phase2SecurityTrustedClear ||
        out.phase2TargetSessionAudioRelease ||
        out.phase2ExplicitClearGrantVoiceRelease ||
        out.phase2TargetEssKnown ||
        out.phase2TargetMacCrcValid;
    if (!phase2Path) {
        return out.decodedFrames > 0 && !out.audio.empty();
    }
    if (out.skippedEncrypted ||
        out.phase2TargetEssEncrypted ||
        out.phase2WrongSlot ||
        out.phase2StaleAudioTail) {
        return false;
    }
    const bool trustedConcealmentOnly = p25Phase2TrustedConcealmentOnlyWindow(out);
    if ((!trustedConcealmentOnly && out.decodedFrames == 0) ||
        out.phase2EmittedPcmFrames == 0 ||
        out.phase2FedToMbelib == 0 ||
        out.phase2TargetVoiceCodewords == 0) {
        return false;
    }
    if (p25Phase2UnsafeMixedSlotAudioWindow(out)) {
        return false;
    }
    if (out.phase2ConcealmentFrames > 0 &&
        out.phase2ConcealmentFrames >= out.phase2EmittedPcmFrames &&
        !trustedConcealmentOnly) {
        return false;
    }
    return p25Phase2BlockHasTrustedClearContext(out);
}


bool p25Phase2MayAppendPlcBlock(const Receiver& rx, const P25VoiceAudioBlock& out) noexcept
{
    // SDRTrunk P25P2AudioModule never synthesizes last-good / fade / opposite-slot
    // PLC.  Once clear it feeds every selected-timeslot AMBE frame into JMBE and
    // emits the codec PCM; companion-slot dwell is silence on that module.
    // Inventing faded last-good frames during opp-slot hops was the 20260720
    // regression (fed=0 emitPcm>0, plc_like duty collapse).  Keep the helper as
    // a hard deny so any residual call sites cannot invent speech.
    (void)rx;
    (void)out;
    return false;
}


bool p25Phase2AudioTailGraceActive(const Receiver& rx) noexcept
{
    const int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto& tail = rx.p25SessionState.audioTail;
    return tail.lastFreshTargetVoiceMs > 0 &&
        (nowMs - tail.lastFreshTargetVoiceMs) <= p25Phase2EffectiveAudioTailGraceMs();
}


void p25Phase2UpdateSessionSustainState(Receiver& rx,
                                               const P25VoiceAudioBlock& out,
                                               qint64 nowMs,
                                               bool speakerEmitted) noexcept
{
    auto& sustain = rx.p25SessionState.sustain;
    if (sustain.sessionStartMs == 0) {
        sustain.sessionStartMs = nowMs;
    }
    sustain.peakDecodedFrames = std::max(sustain.peakDecodedFrames,
        static_cast<long long>(out.decodedFrames));
    sustain.peakPhase2Bursts = std::max(sustain.peakPhase2Bursts,
        static_cast<long long>(out.phase2Bursts));
    sustain.peakPhase2SuperframeBursts = std::max(sustain.peakPhase2SuperframeBursts,
        static_cast<long long>(out.phase2SuperframeBursts));
    sustain.peakPhase2MaskedBursts = std::max(sustain.peakPhase2MaskedBursts,
        static_cast<long long>(out.phase2MaskedBursts));
    sustain.peakPhase2TargetVoiceCodewords = std::max(sustain.peakPhase2TargetVoiceCodewords,
        static_cast<long long>(out.phase2TargetVoiceCodewords));
    if (!out.audio.empty()) {
        sustain.cumulativeAudioSamples += static_cast<long long>(out.audio.size());
    }
    if (out.phase2MaskedBursts >= 1 &&
        out.phase2SuperframeBursts >= 1 &&
        (out.phase2TargetVoiceCodewords > 0 || out.decodedFrames > 0)) {
        sustain.hadBootstrapMaskLock = true;
    }
    if (speakerEmitted) {
        sustain.hadSuccessfulEmit = true;
        sustain.lastEmitMs = nowMs;
        // Survive same-call hop / voice-reset sustain wipe so overlap context
        // stays lock-only after the call has already spoken.
        rx.p25Phase2CallHadSpeakerAudio = true;
        // SDRTrunk only queues P25P2 voice timeslots before current-call
        // security is known. Once selected-slot audio reaches the speaker,
        // any selected pending queue is late-entry/bootstrap residue and must
        // not influence later windows or diagnostics.
        const bool hadSelectedPending =
            rx.p25SessionState.pendingAudio.armed ||
            !rx.p25SessionState.pendingAudio.ambeFrames.empty() ||
            rx.p25Phase2PendingAudioArmed ||
            !rx.p25Phase2PendingAudio.empty();
        if (hadSelectedPending) {
            rx.p25SessionState.pendingAudio = {};
            rx.p25Phase2PendingAudio.clear();
            rx.p25Phase2PendingTalkgroupId = 0;
            rx.p25Phase2PendingAudioArmed = false;
            ++rx.p25DiagPendingAudioCleared;
            if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
                spdlog::info("P25 PENDING_AUDIO_CLEARED reason={} tg={} slot={} session={}",
                             p25PendingClearReasonName(P25PendingClearReason::LiveStreamPreferred),
                             rx.p25VoiceTalkgroupId,
                             static_cast<unsigned>(rx.p25VoiceTdmaSlot & 0x01u),
                             static_cast<unsigned long long>(rx.p25CurrentCallSessionId));
            }
        }
        // Speaker PCM confirms that the playout path worked, but it is not new
        // MAC/ESS/PTT evidence. Keep recent-security proof tied to traffic-side
        // observations so stale clear state cannot release later unknown windows.
        if (rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear ||
            (rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted)) {
            const P25P2CallAudioKey key = p25CurrentPhase2AudioKey(rx, out.effectiveTargetFreqHz);
            const bool targetSessionClearThisWindow =
                out.phase2ThisWindowTargetSessionAudioRelease &&
                !out.phase2TargetEssEncrypted;
            const bool targetEssKnownThisWindow =
                out.phase2ThisWindowTargetEssClear ||
                out.phase2ThisWindowTargetEssEncrypted;
            const bool targetMacThisWindow = out.phase2ThisWindowTargetMacCrcValid;
            const bool anyMacThisWindow = out.phase2MacCrcValid > 0;
            if (targetMacThisWindow || anyMacThisWindow || targetEssKnownThisWindow ||
                targetSessionClearThisWindow) {
                p25RefreshPhase2RecentSecurityEvidence(
                    rx,
                    key,
                    nowMs,
                    targetMacThisWindow,
                    anyMacThisWindow,
                    targetEssKnownThisWindow,
                    out.phase2ThisWindowTargetEssEncrypted,
                    targetSessionClearThisWindow,
                    out.phase2TargetSecurityStateFromPtt,
                    out.phase2SuperframeBursts > 0 && out.phase2MaskedBursts > 0);
            }
            if (rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Unknown &&
                rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted) {
                p25NotePhase2SecurityLatchChange(rx, P25CallSecurityLatch::Clear, "speaker-emit");
            }
        }
    }
    // Sticky mask/SF retained across block-channelize hops can lock onto the
    // wrong epoch (opp-slot dominant). Soft-repair without full CQPSK wipe.
    //
    // DEC-0043 / capture 20260912_020758 TG20201: after clear emits, single
    // wrong-TDMA / companion-only hops (normal TDMA silence or ±1 lock flip)
    // immediately invalidated the sticky epoch → thrash → permanent no-vcw
    // while the same IQ file stayed duty ~0.80. Before the call has spoken,
    // keep immediate invalidate so cold acquisition can escape a bad epoch.
    // After speak, require a short streak (same bar as structureNoTarget).
    const bool oppDominantWrongEpoch =
        out.phase2OppositeVoiceCodewords >= 4 &&
        out.phase2TargetVoiceCodewords == 0 &&
        out.phase2FedToMbelib == 0 &&
        out.phase2WrongSlotVoiceCodewords >= out.phase2OppositeVoiceCodewords / 2;
    const bool callHasSpoken =
        sustain.hadSuccessfulEmit || rx.p25Phase2CallHadSpeakerAudio;
    if (oppDominantWrongEpoch) {
        if (!callHasSpoken) {
            rx.p25VoiceLiveDecoder.invalidatePhase2StickyMaskEpoch();
            rx.p25Phase2OppDominantEpochWindows = 0;
        } else {
            ++rx.p25Phase2OppDominantEpochWindows;
            if (rx.p25Phase2OppDominantEpochWindows >= 3) {
                rx.p25VoiceLiveDecoder.invalidatePhase2StickyMaskEpoch();
                rx.p25Phase2OppDominantEpochWindows = 0;
            }
        }
    } else if (out.phase2TargetVoiceCodewords > 0 || out.phase2FedToMbelib > 0) {
        rx.p25Phase2OppDominantEpochWindows = 0;
    }
    // Capture 20260808_022809: long runs of p2bursts>0 with targetVcw=0 after
    // real emits (structure without selected-slot Voice2/4) — sticky lattice
    // stuck on SACCH/FACCH epoch. Soft rehunt mask/SF without full CQPSK wipe.
    if (sustain.hadSuccessfulEmit &&
        out.phase2Bursts >= 1 &&
        out.phase2TargetVoiceCodewords == 0 &&
        out.phase2OppositeVoiceCodewords == 0 &&
        out.phase2FedToMbelib == 0 &&
        out.decodedFrames == 0) {
        ++rx.p25Phase2StructureNoTargetVoiceWindows;
        if (rx.p25Phase2StructureNoTargetVoiceWindows >= 3) {
            rx.p25VoiceLiveDecoder.invalidatePhase2StickyMaskEpoch();
            rx.p25Phase2ForceMaskEpochRehunt = true;
            rx.p25Phase2StructureNoTargetVoiceWindows = 0;
        }
    } else if (out.phase2TargetVoiceCodewords > 0 || out.phase2FedToMbelib > 0) {
        rx.p25Phase2StructureNoTargetVoiceWindows = 0;
    }
}


bool p25Phase2EstablishedClearNoiseFeedAllowed(const Receiver& rx,
                                                       const P25VoiceAudioBlock& out,
                                                       const P25Phase2Burst& burst,
                                                       bool recentMacEvidenceForCall) noexcept
{
    (void)out;
    (void)recentMacEvidenceForCall;
    if (!p25Phase2CurrentSelectedBurstFeedTrusted(burst)) return false;
    if (burst.macCrcValid || burst.macCrcLock) return true;
    if (burst.maskPhaseLock && burst.grantSlotKnown) return true;
    return p25Phase2AudioTailGraceActive(rx) && burst.grantSlotKnown && burst.maskPhaseLock;
}


void p25Phase2UpdateAudioTailTracker(Receiver& rx, const P25VoiceAudioBlock& out) noexcept
{
    const int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    auto& tail = rx.p25SessionState.audioTail;
    const bool freshTarget = p25Phase2WindowHasFreshTargetEvidence(out);
    const bool fedThisWindow = out.phase2FedToMbelib > 0;
    const bool forwardFed = fedThisWindow &&
        (tail.lastForwardedFedAbsDibit == 0 ||
         out.phase2LastFedAbsDibit > tail.lastForwardedFedAbsDibit + 20u);

    if (p25Phase2WindowDisablesPlayoutBridge(out)) {
        tail.playoutBridgeEligible = false;
    }

    if (p25Phase2UnsafeMixedSlotAudioWindow(out)) {
        ++tail.consecutiveNoForwardFedWindows;
        return;
    }

    if (freshTarget && fedThisWindow && forwardFed) {
        tail.lastFreshTargetVoiceMs = nowMs;
        tail.lastForwardedFedAbsDibit = out.phase2LastFedAbsDibit;
        tail.consecutiveNoForwardFedWindows = 0;
        tail.consecutiveEmptyFeedWindows = 0;
        return;
    }
    if (!fedThisWindow) {
        ++tail.consecutiveEmptyFeedWindows;
    } else if (!forwardFed) {
        ++tail.consecutiveNoForwardFedWindows;
    }
}


bool p25Phase2ShouldMarkStaleAudioTail(const Receiver& rx, const P25VoiceAudioBlock& out) noexcept
{
    if (p25Phase2SessionSpeakerSustainActive(rx)) return false;
    const auto& tail = rx.p25SessionState.audioTail;
    if (tail.lastFreshTargetVoiceMs == 0) return false;
    if (p25Phase2AudioTailGraceActive(rx)) return false;
    if (!p25Phase2WindowHasFreshTargetEvidence(out)) return true;
    if (out.phase2FedToMbelib > 0 && tail.consecutiveNoForwardFedWindows >= 4) return true;
    return tail.consecutiveEmptyFeedWindows >= 4;
}


void p25Phase2FinalizeAudioTailState(Receiver& rx, P25VoiceAudioBlock& out) noexcept
{
    p25Phase2UpdateAudioTailTracker(rx, out);
    out.phase2AudioTailGraceActive = p25Phase2AudioTailGraceActive(rx);
    out.phase2StaleAudioTail = p25Phase2ShouldMarkStaleAudioTail(rx, out);
}


std::string p25VoiceBlockSpeakerGateReason(const P25VoiceAudioBlock& out)
{
    if (out.audio.empty()) return "empty-audio";
    const bool trustedConcealmentOnly = p25Phase2TrustedConcealmentOnlyWindow(out);
    if (out.decodedFrames == 0 && !trustedConcealmentOnly) return "no-decoded-frames";
    if (out.diag != P25VoiceDiagCode::Decoding && !trustedConcealmentOnly) return "diag-not-decoding";
    if (out.skippedEncrypted) return "skipped-encrypted";
    if (out.waitingForClearGrant) return "waiting-clear-grant";

    const bool phase2Voice = out.phase2VoiceCodewords > 0 || out.phase2Bursts > 0;
    if (phase2Voice) {
        // Require real mbelib PCM before opening the speaker.  Pending-queue drains
        // and weak mask hits were producing audible blips without sustained VCW feed.
        // Clear-trusted calls may emit on the first accepted AMBE frame so short
        // PTTs are not lost waiting for a second 20 ms frame.
        const size_t minMbelibFrames = out.phase2SecurityTrustedClear
            ? kP25Phase2PendingReleaseMinFrames
            : kP25Phase2UnknownGrantAudioProbeMinFrames;
        if (out.phase2EmittedPcmFrames == 0 &&
            out.phase2FedToMbelib < static_cast<long long>(minMbelibFrames)) {
            return "phase2-no-mbelib-pcm";
        }
        // Live Phase-2 clear windows often produce accepted AMBE PCM before
        // target-slot counters catch up (field logs: decoded>0, targetVcw=0,
        // gate=emit under the prior path).  Do not mute already-accepted PCM;
        // the security gate already dropped encrypted/unknown.  Only require
        // target/tail evidence when this window has no usable PCM yet.
        const bool haveUsablePcm =
            out.phase2EmittedPcmFrames > 0 &&
            !out.audio.empty() &&
            (out.decodedFrames > 0 || trustedConcealmentOnly);
        const bool freshTargetEvidence = p25Phase2WindowHasFreshTargetEvidence(out);
        // Hard SDRTrunk guard: speaker PCM must come from mbelib feed of the
        // selected timeslot.  fed=0 + emitPcm>0 was the invented-PLC regression.
        if (haveUsablePcm && out.phase2FedToMbelib == 0) {
            return "phase2-no-fresh-feed";
        }
        // Match SDRTrunk: only selected-timeslot codec PCM may open the speaker.
        // Do not treat invented last-good / opposite-slot PLC as usable audio.
        if (haveUsablePcm &&
            out.phase2OppositeVoiceCodewords > 0 &&
            p25Phase2UnsafeMixedSlotAudioWindow(out)) {
            return out.phase2TargetVoiceCodewords == 0
                ? "phase2-opposite-slot-without-target"
                : (p25Phase2DualSlotUntrustedGarbleWindow(out)
                    ? "phase2-dual-slot-untrusted-garble"
                    : "phase2-mixed-slot-order-issue");
        }
        // SDRTrunk P25P2AudioModule has no post-decode speaker mute after
        // encrypted-state is established and the call is clear. processAudio()
        // already addAudio()'d every selected-slot JMBE frame. This extra
        // current-window proof mute drops that PCM (fed>0 emitPcm>0).
        if (haveUsablePcm &&
            out.phase2FedToMbelib > 0 &&
            out.phase2SecurityTrustedClear &&
            !out.skippedEncrypted &&
            !out.phase2TargetEssEncrypted) {
            if (!p25AudioSamplesLookSafe(out.audio)) return "audio-samples-not-safe";
            return "emit";
        }
        if (haveUsablePcm &&
            out.phase2SecurityTrustedClear &&
            !p25Phase2TargetHardClearEvidence(out) &&
            !out.phase2SdrtrunkLateEntryVoiceRelease &&
            !out.phase2ExplicitClearGrantVoiceRelease) {
            return "phase2-no-current-target-clear-proof";
        }
        // Clear-trusted selected-slot PCM with a few concealment frames is still
        // real speech; muting the whole window for concealment-dominant produced
        // blocky holes (20260807_234054 gate=phase2-concealment-dominant).
        if (haveUsablePcm &&
            out.phase2ConcealmentFrames > 0 &&
            out.phase2ConcealmentFrames >= out.phase2EmittedPcmFrames &&
            !out.phase2SecurityTrustedClear &&
            !p25Phase2BlockHasTrustedClearContext(out)) {
            return "phase2-concealment-dominant";
        }
        if (haveUsablePcm &&
            out.phase2ConcealmentFrames > 0 &&
            !freshTargetEvidence &&
            !out.phase2AudioTailGraceActive) {
            return "phase2-concealment-without-target";
        }
        if (haveUsablePcm &&
            !freshTargetEvidence &&
            !p25Phase2BlockHasTrustedClearContext(out)) {
            return "phase2-no-fresh-target-voice";
        }
        if (haveUsablePcm &&
            !freshTargetEvidence &&
            out.phase2WrongSlotVoiceCodewords > 0 &&
            !p25Phase2BlockHasTrustedClearContext(out)) {
            return "phase2-fragmented-tail-muted";
        }
        // Stale-tail must never mute a window that already produced usable PCM.
        // Field/stream tests showed decoded=2 + audio samples blocked by
        // phase2-stale-audio-tail, chopping continuous speech into blips.
        if (out.phase2StaleAudioTail && !haveUsablePcm) return "phase2-stale-audio-tail";
        if (!haveUsablePcm &&
            !out.phase2AudioTailGraceActive &&
            !freshTargetEvidence) {
            return "phase2-no-target-slot-vcw";
        }
        if (!haveUsablePcm &&
            !freshTargetEvidence &&
            !out.phase2AudioTailGraceActive) {
            return "phase2-no-fresh-target-voice";
        }
        if (!haveUsablePcm &&
            out.phase2FedToMbelib == 0 &&
            !out.phase2AudioTailGraceActive) {
            return "phase2-no-fresh-feed";
        }
    }

    if (out.phase2AudioLockMissing) return "phase2-audio-lock-missing";
    if (out.phase2MetadataMissing) return "phase2-metadata-missing";
    if (out.phase2MaskMissing) return "phase2-mask-missing";
    if (out.phase2MaskAppliedNoMacCrc) return "phase2-mask-applied-no-mac-crc";
    if (out.phase2EssMissing) return "phase2-ess-missing";
    if (out.phase2LateEntryWaiting) return "phase2-late-entry-waiting";
    if (out.phase2VoiceUnsupported) return "phase2-voice-unsupported";
    // Variant churn is diagnostic only; never mute an otherwise clear window.
    // A mixed Phase-2 late-entry window can contain valid followed-slot voice and
    // rejected opposite-slot VCWs at the same time.  Opposite-slot dominance is
    // diagnostic only once target-slot AMBE has already been selected and decoded;
    // only true wrong-slot/order failures remain speaker-blocking.
    if (!p25AudioSamplesLookSafe(out.audio)) return "audio-samples-not-safe";
    return "emit";
}


bool p25VoiceBlockMayEmitAudio(const P25VoiceAudioBlock& out)
{
    return p25VoiceBlockSpeakerGateReason(out) == "emit";
}


bool p25VoiceBlockMayBypassPostArmSettle(const P25VoiceAudioBlock& out,
                                                const std::string& rawSpeakerGateReason) noexcept
{
    if (rawSpeakerGateReason != "emit") return false;
    const bool phase2Path =
        out.phase2VoiceCodewords > 0 ||
        out.phase2Bursts > 0 ||
        out.phase2SecurityTrustedClear ||
        out.phase2SecurityUnknown ||
        out.phase2ExplicitClearGrantVoiceRelease ||
        out.phase2TargetSessionAudioRelease ||
        out.phase2TargetEssKnown ||
        out.phase2TargetMacCrcValid;
    if (!phase2Path) return true;

    // Explicit clear grants may follow and queue/decode immediately, but the
    // speaker warmup may only be bypassed after traffic-side proof from the
    // followed TDMA slot. This blocks the first unstable AMBE acquire slice.
    if (p25Phase2TargetHardClearEvidence(out)) return true;
    if (out.phase2SdrtrunkLateEntryVoiceRelease &&
        out.phase2TargetMacCrcValid &&
        out.phase2MacCrcValid > 0 &&
        !out.phase2TargetEssEncrypted) {
        return true;
    }
    return false;
}


void p25ClearPhase2SpeakerMuteFlags(P25VoiceAudioBlock& out) noexcept
{
    out.waitingForClearGrant = false;
    out.phase2AudioLockMissing = false;
    out.phase2MetadataMissing = false;
    out.phase2MaskMissing = false;
    out.phase2MaskAppliedNoMacCrc = false;
    out.phase2EssMissing = false;
    out.phase2WrongSlot = false;
    out.phase2AmbeRejected = false;
    out.phase2AmbeVariantUnstable = false;
    out.phase2LateEntryWaiting = false;
    out.phase2VoiceUnsupported = false;
    out.diag = P25VoiceDiagCode::Decoding;
}


P25VoiceAudioBlock applyP25Phase2SecurityAudioGate(Receiver& rx,
                                                            P25VoiceAudioBlock out,
                                                            double targetFreqHz)
{
    if (!rx.p25VoicePhase2) return out;

    // =====================================================================
    // SDRTRUNK-ALIGNED DEFINED CASES + EDGE CASES FOR P25 PHASE 2 AUDIO RELEASE (STRICT)
    // Only emit when one of these proves the call clear. DO NOT relax.
    // All pipeline (320-bit timeslot extract, mask, ACCH/MAC/ESS, voice offsets/packing,
    // interleave to mbelib, slot, follow) must use SDRTrunk logic.
    // Cases (cross-ref P25P2MessageProcessor, Voice*Timeslot, P25P2AudioModule, ScramblingSequence):
    // 1. burst.sessionAudioRelease && !encrypted : maskApplied + essKnown+Trusted+fec + (pttSeen from MAC_PTT or ess fec) + !enc
    // 2. burst.macCrcLock (crcValid || (fecDecoded && correctedSymbols < 10)) + MAC_PTT/MAC_ACTIVE with clear ESS
    // 3. targetEssKnown && !targetEssEncrypted (after valid PTT/ESS-A RS)
    // 4. Explicit clear control grants may follow/queue the call, but they do
    //    not release speaker audio until the traffic slot itself proves clear
    //    via target-slot PTT/ESS/session state. Encrypted ESS or wrong-slot
    //    evidence always overrides and mutes.
    // 5. sdrtrunkLateEntryVoiceRelease or lateEntryStrongTargetReleaseAllowed ONLY when target MAC/ESS proves clear plus strong target VCW + superframe/mask/slot.
    //    (unknown late entry still requires proof MAC or ESS; VCW count alone never suffices for emit)
    //
    // Edge cases fully defined/handled:
    // - Late entry (no superframe or no initial MAC on first bursts): follow with mask from grant, queue raw AMBE, emit only after first PTT/ESS or defined strong+mac proof. No dummy emit.
    // - Voice-only bursts (DUID=Voice4/2, no ACCH this slot): carry session state (pttSeen/activeSeen persist until END/IDLE/HANG); use carried for release.
    // - No superframe lock: still release if grantSlotKnown + maskApplied + (macCrcLock or sessionAudioRelease or target ess clear)
    // - Slot mismatch (grant vs observed superframeBurstIndex): reject unless !grantKnown && late-probe; opposite slot VCW ignored for release.
    // - Encrypted grant or ESS alg!=0x80: always drop (trustedEncrypted).
    // - FEC vs CRC: low-fec MAC (<10 corrected) accepted for state/lock (SDRTrunk recovers MAC for call state); final emit still prefers crcValid or ess fec.
    // - Unknown/pending grant: follow+diagnostic decode/queue, but gate blocks emit (unknownSecurity -> clear audio, wait for PTT/ESS).
    // - Mask phase no lock but sticky from prior MAC/ESS/voice: allowed for decode if target followed; emit still requires proof.
    // - ISCH anchors mask phase when no MAC (code uses location/channel vs grantSlot).
    // - Wrong slot: phase2TrafficSlotFromSuperframeBurstIndex keeps the grant slot authoritative.
    // - Superframe/mask high but p2mac=0: with status-strip fix, MAC should recover on real traffic; gate still waits for proof.
    // - Concealment: mbelib state persists; only emit low-err usable frames.
    // - No force/dummy: removed; pending queue only for unknown until proof.
    // =====================================================================

    const P25P2CallAudioKey key = p25CurrentPhase2AudioKey(rx, targetFreqHz);

    // Fail closed on an explicit encrypted grant, but otherwise use the followed
    // traffic-slot's own PTT/ESS/voice-session state for speaker release. A
    // control-channel clear service option is not enough to synthesize audio:
    // sdrtrunk queues AbstractVoiceTimeslot instances until PTT/ESS establishes
    // clear/encrypted state. The aggregate live.stats ESS/MAC counters can
    // describe the other slot on the same Phase-2 carrier and must not flush/drop
    // the target call queue.
    const auto latchBefore = rx.p25SessionState.callSecurityLatch;
    const bool latchClear = latchBefore == P25CallSecurityLatch::Clear;
    const bool latchEncrypted = latchBefore == P25CallSecurityLatch::Encrypted;
    // Sticky recent ESS/encrypted flags must not kill a latched-clear call.
    // SDRTrunk only flips on a valid PTT or valid ESS in the current timeslot.
    const bool thisWindowEssEncryptedClaim =
        out.phase2ThisWindowTargetEssEncrypted ||
        (out.phase2TargetSecurityStateFromPtt && out.phase2TargetEssEncrypted);
    // DEC-0025 / capture 20260908_103955 TG20202 RID 0x1F95EB: ReturnEncrypted
    // while follow/DEEP DIAG still ess=clear after clear PCM. Follow SM's
    // trustedEncryptedEss already requires macCrc>0; security-gate did not, so
    // Clear→Encrypted latch made grantProvesEncrypted true and killed the call.
    // File voicetest of the same IQ: PASS_CONTINUOUS essEncrypted=no.
    const bool thisWindowObservedEncrypted =
        thisWindowEssEncryptedClaim &&
        (out.phase2TargetMacCrcValid ||
         out.phase2MacCrcValid > 0 ||
         out.phase2TargetSecurityStateFromPtt);
    const bool windowEncrypted = latchEncrypted ||
        thisWindowObservedEncrypted ||
        (!latchClear &&
         (rx.p25VoiceEncrypted ||
          (out.phase2TargetEssKnown && out.phase2TargetEssEncrypted)));
    // Late-entry release is trusted when target-slot ESS/session proves clear.
    // Control-channel clear state may select/follow the call, but it cannot turn
    // encrypted or wrong-slot AMBE-shaped payload into speaker audio.
    const bool sdrtrunkLateEntryVoiceRelease =
        out.phase2SdrtrunkLateEntryVoiceRelease ||
        p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out);
    // decodeP25Phase2VoiceBlock already required target-slot ESS/PTT (or an
    // established clear sustain/tail) before setting
    // phase2ExplicitClearGrantVoiceRelease. Re-checking hard-clear evidence here
    // double-gated the first clear windows and muted PCM that the feed path had
    // already accepted — match SDRTrunk: once the timeslot security session is
    // established clear, keep the speaker open for that grant.
    const bool explicitClearGrantVoiceRelease =
        out.phase2ExplicitClearGrantVoiceRelease &&
        (rx.p25VoiceClearKnown || latchClear) &&
        !rx.p25VoiceEncrypted &&
        !out.phase2TargetEssEncrypted &&
        !out.phase2WrongSlot;
    const bool unknownGrantProbeVoiceRelease =
        p25Phase2UnknownGrantProbeVoiceReleaseEvidence(rx, out);
    // Fresh window proof (excludes latch + diagnostic-only probe).
    const bool windowFreshClear = !windowEncrypted &&
        (p25Phase2TargetHardClearEvidence(out) ||
         sdrtrunkLateEntryVoiceRelease ||
         explicitClearGrantVoiceRelease);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentTargetClearForCall =
        p25Phase2RecentSecurityEvidenceUsable(rx, key, nowMs) &&
        (rx.p25Phase2RecentTargetSessionAudioRelease ||
         (rx.p25Phase2RecentTargetEssKnown && !rx.p25Phase2RecentTargetEssEncrypted));
    const bool sameCallRecentClearSustain =
        latchClear &&
        recentTargetClearForCall &&
        !windowEncrypted &&
        !out.phase2TargetEssEncrypted &&
        !out.phase2WrongSlot &&
        p25Phase2WindowHasFreshTargetEvidence(out);
    // Monotonic latch: Unknown -> Clear / Encrypted only. After Clear, do not
    // require each window to re-prove clear (no MAC/ESS/mask/quality thrash).
    // Diagnostic-only unknown-grant probes must not latch Clear.
    if (windowEncrypted) {
        p25NotePhase2SecurityLatchChange(rx, P25CallSecurityLatch::Encrypted, "security-gate");
        p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::EncryptedState);
        p25ClearPhase2RecentSecurityEvidence(rx);
    } else if (windowFreshClear) {
        p25NotePhase2SecurityLatchChange(rx, P25CallSecurityLatch::Clear, "security-gate");
    }
    const bool trustedEncrypted =
        rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Encrypted ||
        windowEncrypted;
    // Monotonic Clear latch is same-call proof only. Capture 20260808_021134:
    // latch alone + dual-slot ess=unknown trusted-clear-released garble; require
    // no dual-slot-untrusted and either fresh traffic proof or clean structure.
    // DEC-0031 / DEC-0003 / capture 20260909_062006: once the call has spoken
    // or latched Clear, do not require *this window* to already have fed PCM
    // before continuation can defeat dual-slot untrusted. requireFedAudio=true
    // was a chicken-egg: dual-slot mute cleared audio → continuation false →
    // trustedClear collapsed to unknown-waiting-clear on the next hop.
    const bool onceClearCall =
        latchClear ||
        rx.p25SessionState.sustain.hadSuccessfulEmit ||
        rx.p25Phase2CallHadSpeakerAudio;
    const bool sameCallSelectedContinuation =
        p25Phase2SameCallSelectedTimeslotContinuationSafe(
            rx, out, key, nowMs, /*requireFedAudio=*/!onceClearCall);
    out.phase2SameCallSelectedTimeslotContinuation =
        out.phase2SameCallSelectedTimeslotContinuation || sameCallSelectedContinuation;
    const bool dualSlotUntrustedGate =
        p25Phase2DualSlotUntrustedGarbleWindow(out) &&
        !sameCallSelectedContinuation;
    // DEC-0058 / 142104 TG10301: after Clear latch + speak, dual-slot MAC-dead
    // hops with sticky ess=clear still DualSlotUntrusted (no this-window ESS
    // observation) → feed starve + waiting-clear islands between emits. When
    // selected is dominant/equal, companion is accounted, and target ESS is
    // already known clear, do not brand the hop as garble (DEC-0012 still
    // covers companion-louder PostEmitMixedMacDead on the feed path).
    const bool latchedSelectedDominantClearContinuation =
        onceClearCall &&
        out.phase2TargetEssKnown &&
        !out.phase2TargetEssEncrypted &&
        !out.phase2WrongSlot &&
        out.phase2TargetVoiceCodewords >= 2 &&
        out.phase2TargetVoiceCodewords >= out.phase2OppositeVoiceCodewords &&
        p25Phase2CompanionSlotAccounted(out) &&
        p25Phase2StrongSelectedSlotStructure(out);
    const bool dualSlotUntrustedGateEffective =
        dualSlotUntrustedGate && !latchedSelectedDominantClearContinuation;
    // DEC-0012: fail-close trustedClear on post-emit mixed MAC-dead so
    // explicit-clear / ESS-only cannot emit leftover unproven PCM.  The only
    // escape is SDRTrunk-style selected-slot continuation: this window already
    // fed current, labelled target PCM and the companion slot is accounted for.
    // Do not fold this into dualSlotUntrustedGate — that path audio.clear()s
    // the hop (105622 duty 0.38).
    const bool postEmitSelectedContinuationSafe =
        p25Phase2PostEmitSelectedSlotContinuationSafe(
            rx, out, key, nowMs, /*requireFedAudio=*/true);
    const bool postEmitMixedMacDeadGate =
        p25Phase2PostEmitMixedMacDeadWindow(rx, out) &&
        !postEmitSelectedContinuationSafe;
    const bool thisWindowTargetSessionClear =
        out.phase2ThisWindowTargetSessionAudioRelease &&
        !out.phase2TargetEssEncrypted;
    const bool latchClearSameCallSafe =
        latchClear &&
        !dualSlotUntrustedGateEffective &&
        !out.phase2WrongSlot &&
        !out.phase2TargetEssEncrypted &&
        (windowFreshClear ||
         sameCallRecentClearSustain ||
         out.phase2ThisWindowTargetMacCrcValid ||
         out.phase2ThisWindowTargetEssClear ||
         thisWindowTargetSessionClear ||
         (recentTargetClearForCall &&
          out.phase2SuperframeBursts > 0 && out.phase2MaskedBursts > 0 &&
          out.phase2TargetVoiceCodewords > 0 &&
          out.phase2OppositeVoiceCodewords == 0));
    // Capture 20260808_034136: explicit-clear-grant and sameCallRecentClear
    // bypassed dual-slot MAC-dead mute and released ~38s blocky PCM.  Dual-slot
    // untrusted must fail-close every trustedClear path, not only the latch path.
    const bool trustedClear =
        !trustedEncrypted &&
        !dualSlotUntrustedGateEffective &&
        !postEmitMixedMacDeadGate &&
        (latchClearSameCallSafe ||
         sameCallRecentClearSustain ||
         postEmitSelectedContinuationSafe ||
         windowFreshClear ||
         explicitClearGrantVoiceRelease ||
         (rx.p25SessionState.sustain.hadSuccessfulEmit &&
          (rx.p25VoiceClearKnown || latchClear) &&
          !out.phase2WrongSlot &&
          !out.phase2TargetEssEncrypted &&
           p25Phase2WindowHasFreshTargetEvidence(out) &&
           (recentTargetClearForCall ||
            out.phase2ThisWindowTargetMacCrcValid ||
            out.phase2ThisWindowTargetEssClear ||
           thisWindowTargetSessionClear ||
           latchedSelectedDominantClearContinuation) &&
          (out.phase2OppositeVoiceCodewords == 0 ||
           out.phase2ThisWindowTargetMacCrcValid ||
           out.phase2ThisWindowTargetEssClear ||
           latchedSelectedDominantClearContinuation)) ||
         unknownGrantProbeVoiceRelease);
    const bool trustedClearPendingRelease =
        trustedClear &&
        key.valid() &&
        p25Phase2PendingAudioMatches(rx, key) &&
        out.phase2PendingAmbeFramesReleased >= kP25Phase2PendingReleaseMinFrames;
    // Never fall back to Unknown once latched Clear/Encrypted for this call.
    const bool unknownSecurity = !trustedClear && !trustedEncrypted;
    auto pendingSamplesForKey = [&]() -> size_t {
        return key.valid() ? p25Phase2PendingAudioSampleCount(rx, key) : 0u;
    };
    out.phase2PreSecurityAudioSamples = out.audio.size();
    out.phase2PreSecurityDecodedFrames = out.decodedFrames;
    out.phase2PendingAudioSamplesBefore = pendingSamplesForKey();
    out.phase2SecurityTrustedClear = trustedClear;
    out.phase2SecurityTrustedEncrypted = trustedEncrypted;
    out.phase2SecurityUnknown = unknownSecurity;
    out.phase2SecurityGateAction = "pass-through";
    auto finishSecurityGate = [&](const char* action) -> P25VoiceAudioBlock {
        out.phase2SecurityGateAction = action ? action : "";
        out.phase2PendingAudioSamplesAfter = pendingSamplesForKey();
        return out;
    };

    if (!key.valid()) {
        p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::CallIdentityChanged);
    }

    if (trustedEncrypted) {
        out.audio.clear();
        out.decodedFrames = 0;
        out.skippedEncrypted = true;
        out.diag = P25VoiceDiagCode::SkippedEncrypted;
        return finishSecurityGate("trusted-encrypted-drop");
    }

    // Dual-slot MAC-dead: default drops this window's PCM.  DEC-0055.1: once the
    // call is latched Clear and this window already fed labelled selected VCWs,
    // keep that PCM (feed path still blocks further dual-slot bursts).  Do not
    // fall through to unknownSecurity clear — that punched continuous-audio
    // holes after the first clear second (061217-class).
    if (dualSlotUntrustedGateEffective) {
        const bool keepLabelledSelectedClearPcm =
            latchClear &&
            out.phase2TargetVoiceCodewords > 0 &&
            !out.phase2WrongSlot &&
            !out.phase2TargetEssEncrypted &&
            !out.audio.empty() &&
            out.phase2FedToMbelib > 0 &&
            (out.phase2CurrentFeedTrustedTargetBurst ||
             p25Phase2StrongSelectedSlotStructure(out));
        if (!keepLabelledSelectedClearPcm) {
            out.audio.clear();
            out.decodedFrames = 0;
            out.phase2SecurityTrustedClear = false;
            out.phase2CurrentProbePcmUsable = false;
            out.phase2UnknownProbeQualityOk = false;
            out.phase2UnknownProbeBlockReason = "dual-slot-mac-dead-untrusted";
            out.diag = P25VoiceDiagCode::Decoding;
            return finishSecurityGate("dual-slot-untrusted-garble-drop");
        }
        out.phase2SecurityTrustedClear = true;
        out.phase2SecurityUnknown = false;
        out.phase2CurrentProbePcmUsable = false;
        out.phase2UnknownProbeQualityOk = false;
        out.phase2UnknownProbeBlockReason = "dual-slot-mac-dead-keep-selected-pcm";
        out.diag = P25VoiceDiagCode::Decoding;
        p25ClearPhase2SpeakerMuteFlags(out);
        return finishSecurityGate("dual-slot-untrusted-keep-selected-pcm");
    }

    if (unknownSecurity) {
        // Match sdrtrunk P25P2AudioModule / Current Call Security Session for
        // ordinary unknown calls: queue raw AMBE and keep speaker muted until
        // PTT/ESS or target traffic proof arrives. Diagnostic probes never
        // release speaker audio.
        out.audio.clear();
        out.decodedFrames = 0;
        out.phase2CurrentProbePcmUsable = false;
        out.phase2UnknownProbeQualityOk = false;
        out.phase2UnknownProbeBlockReason = out.phase2FieldAudioProbeAllowed
            ? "late-entry-audio-probe-diagnostic-only"
            : "waiting-ptt-ess";
        out.phase2FieldAudioProbeAllowed = false;
        out.phase2SdrtrunkLateEntryVoiceRelease = false;
        out.waitingForClearGrant = true;
        out.phase2MetadataMissing = true;
        out.phase2LateEntryWaiting = true;
        out.diag = P25VoiceDiagCode::WaitingForClearGrant;
        return finishSecurityGate(key.valid() && p25Phase2PendingAudioMatches(rx, key)
            ? "unknown-raw-queued-waiting-clear"
            : "unknown-waiting-clear");
    }
    if (trustedClear && ((!out.audio.empty() && out.decodedFrames > 0) ||
                         (trustedClearPendingRelease &&
                          out.phase2FedToMbelib >= static_cast<long long>(kP25Phase2PendingReleaseMinFrames)))) {
        p25ClearPhase2SpeakerMuteFlags(out);
    }

    return finishSecurityGate(trustedClear
        ? (sameCallRecentClearSustain && !windowFreshClear
            ? "recent-clear-sustain"
            : (trustedClearPendingRelease
            ? "trusted-clear-pending-release"
            : (explicitClearGrantVoiceRelease
                ? "explicit-clear-grant-traffic-clear-release"
                : (unknownGrantProbeVoiceRelease
                    ? "unknown-grant-probe-diagnostic-only"
                    : (sdrtrunkLateEntryVoiceRelease
                    ? (out.phase2UnknownProbeQualityOk
                        ? "late-entry-strong-target-release"
                        : "target-traffic-clear-release")
                    : "trusted-clear-release")))))
        : "pass-through");
}




bool p25HasValidatedNid(const P25LiveDecodeResult& live)
{
    if (live.stats.bestNidValid) return true;
    return std::any_of(live.nids.begin(), live.nids.end(), [](const P25Nid& nid) {
        return nid.fecValidated;
    });
}


P25VoiceDiagCode chooseP25VoiceDiag(const P25VoiceAudioBlock& out)
{
    if (out.skippedEncrypted) return P25VoiceDiagCode::SkippedEncrypted;
    // Once AMBE has produced usable PCM, report the window as decoding.  A
    // Phase-2 overlap/late-entry window can also contain rejected opposite-slot
    // VCWs; letting that diagnostic outrank decoded audio made the GUI auto-probe
    // flip away from a slot that was already talking.
    if (out.decodedFrames > 0 && !out.audio.empty()) return P25VoiceDiagCode::Decoding;
    // Prefer concrete RF/decoder failures over generic "waiting clear grant".
    // Field captures with clear grants + p2sf/p2mask/p2vcw high were mislabeled
    // as waiting-clear because waitingForClearGrant outranked mask/AMBE reasons.
    if (out.phase2WrongSlot) return P25VoiceDiagCode::Phase2WrongSlot;
    if (out.phase2AmbeRejected) return P25VoiceDiagCode::Phase2AmbeRejected;
    if (out.phase2MaskAppliedNoMacCrc) return P25VoiceDiagCode::Phase2MaskAppliedNoMacCrc;
    if (out.phase2MaskMissing) return P25VoiceDiagCode::Phase2MaskMissing;
    if (out.phase2EssMissing) return P25VoiceDiagCode::Phase2EssMissing;
    if (out.phase2LateEntryWaiting) return P25VoiceDiagCode::Phase2LateEntryWaiting;
    if (out.phase2AudioLockMissing) return P25VoiceDiagCode::Phase2AudioLockMissing;
    if (out.phase2MetadataMissing) return P25VoiceDiagCode::Phase2MetadataMissing;
    if (out.phase2VoiceUnsupported) return P25VoiceDiagCode::Phase2Unsupported;
    if (out.waitingForClearGrant) return P25VoiceDiagCode::WaitingForClearGrant;
    if (out.syncs == 0 && out.phase2Bursts == 0) return P25VoiceDiagCode::NoSync;
    if (out.nids > 0 && !out.nidLock) return P25VoiceDiagCode::NidUnlocked;
    if (!out.backendAvailable && (out.imbeFrames > 0 || out.phase2VoiceCodewords > 0)) return P25VoiceDiagCode::BackendMissing;
    if (out.imbeFrames == 0 && out.phase2VoiceCodewords == 0) return P25VoiceDiagCode::NoLduVoice;
    if (out.decodedFrames == 0) return P25VoiceDiagCode::NoDecodedAudio;
    return P25VoiceDiagCode::Decoding;
}


bool p25Phase2ShouldFlushStaleVoicePipeline(const P25VoiceAudioBlock& out) noexcept
{
    // Do not flush an active Phase-2 traffic session just because one DSP
    // window misses sync.  Field logs show occasional isolated no-sync windows
    // in the middle of a valid call; the old flush erased the rolling IQ buffer
    // and jumped the receiver cursor to live edge, which guaranteed a long
    // audio hole and often prevented re-lock.  Call end/return-to-control and
    // explicit reset paths already perform authoritative cleanup.
    if (out.talkgroupId != 0) return false;
    return out.diag == P25VoiceDiagCode::NoSync &&
        out.decodedFrames == 0 &&
        out.audio.empty() &&
        out.phase2Bursts == 0 &&
        out.phase2VoiceCodewords == 0 &&
        out.phase2SuperframeBursts == 0 &&
        out.phase2MaskedBursts == 0;
}


bool p25Phase2ShouldFlushAudioTail(const P25VoiceAudioBlock& out) noexcept
{
    if (!out.phase2StaleAudioTail) return false;
    // Stale-tail mutes audio, but it must not tear down an active followed TG's
    // RF rolling cursor.  The latest field capture showed the first valid
    // gate=emit followed by an unknown security window; flushing here erased
    // overlap/context and jumped the traffic receiver to live edge, preventing
    // re-lock.  Authoritative call-end/return-to-control paths still clear the
    // pipeline when the grant actually ends.
    if (out.talkgroupId != 0) return false;
    return out.phase2Bursts == 0 &&
        out.phase2VoiceCodewords == 0 &&
        out.phase2SuperframeBursts == 0 &&
        out.phase2MaskedBursts == 0;
}


P25VoiceDiagSnapshot makeP25VoiceDiagnostics(const P25VoiceAudioBlock& out)
{
    P25VoiceDiagSnapshot diag;
    diag.diag = static_cast<int>(out.diag);
    diag.talkgroupId = out.talkgroupId;
    diag.syncs = static_cast<long long>(out.syncs);
    diag.nids = static_cast<long long>(out.nids);
    diag.imbeFrames = static_cast<long long>(out.imbeFrames);
    diag.decodedFrames = static_cast<long long>(out.decodedFrames);
    diag.audioSamples = static_cast<long long>(out.audio.size());
    diag.phase2Bursts = static_cast<long long>(out.phase2Bursts);
    diag.phase2VoiceCodewords = static_cast<long long>(out.phase2VoiceCodewords);
    diag.phase2TargetVoiceCodewords = static_cast<long long>(out.phase2TargetVoiceCodewords);
    diag.phase2OppositeVoiceCodewords = static_cast<long long>(out.phase2OppositeVoiceCodewords);
    diag.phase2ExpectedVoiceCodewords = static_cast<long long>(out.phase2ExpectedVoiceCodewords);
    diag.phase2FedToMbelib = static_cast<long long>(out.phase2FedToMbelib);
    diag.phase2EmittedPcmFrames = static_cast<long long>(out.phase2EmittedPcmFrames);
    diag.phase2FeedGaps = static_cast<long long>(out.phase2FeedGaps);
    diag.phase2AmbeDecodeAttempts = static_cast<long long>(out.phase2AmbeDecodeAttempts);
    diag.phase2AmbeAcceptedFrames = static_cast<long long>(out.phase2AmbeAcceptedFrames);
    diag.phase2DiagnosticAmbeProbeAttempts = static_cast<long long>(out.phase2DiagnosticAmbeProbeAttempts);
    diag.phase2DiagnosticAmbeProbeAccepted = static_cast<long long>(out.phase2DiagnosticAmbeProbeAccepted);
    diag.phase2DuplicateSuppressedVoiceCodewords = static_cast<long long>(out.phase2DuplicateSuppressedVoiceCodewords);
    diag.phase2SuperframeBursts = static_cast<long long>(out.phase2SuperframeBursts);
    diag.phase2MaskedBursts = static_cast<long long>(out.phase2MaskedBursts);
    diag.phase2MacPdus = static_cast<long long>(out.phase2MacPdus);
    diag.phase2MacCrcValid = static_cast<long long>(out.phase2MacCrcValid);
    diag.phase2MacFecDecoded = static_cast<long long>(out.phase2MacFecDecoded);
    diag.phase2MacDirectCrcValid = static_cast<long long>(out.phase2MacDirectCrcValid);
    diag.phase2MacDirectCrcRejected = static_cast<long long>(out.phase2MacDirectCrcRejected);
    diag.phase2MacRsDecoded = static_cast<long long>(out.phase2MacRsDecoded);
    diag.phase2MacNominalCrcValid = static_cast<long long>(out.phase2MacNominalCrcValid);
    diag.phase2MacAltKindCrcValid = static_cast<long long>(out.phase2MacAltKindCrcValid);
    diag.phase2MacBitSwapCrcValid = static_cast<long long>(out.phase2MacBitSwapCrcValid);
    diag.phase2MacSlipCrcValid = static_cast<long long>(out.phase2MacSlipCrcValid);
    diag.phase2MacInvertCrcValid = static_cast<long long>(out.phase2MacInvertCrcValid);
    diag.phase2EssKnown = out.phase2EssKnown;
    diag.phase2EssEncrypted = out.phase2EssEncrypted;
    diag.phase2TargetEssKnown = out.phase2TargetEssKnown;
    diag.phase2TargetEssEncrypted = out.phase2TargetEssEncrypted;
    diag.phase2TargetMacCrcValid = out.phase2TargetMacCrcValid;
    diag.phase2TargetSessionAudioRelease = out.phase2TargetSessionAudioRelease;
    diag.phase2TargetSecurityStateFromPtt = out.phase2TargetSecurityStateFromPtt;
    diag.phase2CurrentFeedTrustedTargetBurst = out.phase2CurrentFeedTrustedTargetBurst;
    diag.phase2SameCallSelectedTimeslotContinuation = out.phase2SameCallSelectedTimeslotContinuation;
    diag.backendAvailable = out.backendAvailable;
    diag.nidLock = out.nidLock;
    diag.phase2CenterFreqHz = out.centerFreqHz;
    diag.phase2EffectiveTargetFreqHz = out.effectiveTargetFreqHz;
    return diag;
}


int boundedJsonInt(size_t value) noexcept
{
    return static_cast<int>(std::min<size_t>(value, static_cast<size_t>(std::numeric_limits<int>::max())));
}


QJsonObject p25RemoteAudioMetrics(const std::vector<float>& audio)
{
    QJsonObject metrics;
    metrics["samples"] = boundedJsonInt(audio.size());
    if (audio.empty()) {
        metrics["rms"] = 0.0;
        metrics["peak"] = 0.0;
        metrics["nonFinite"] = 0;
        return metrics;
    }
    double sumSquares = 0.0;
    double peak = 0.0;
    int nonFinite = 0;
    for (const float sample : audio) {
        if (!std::isfinite(sample)) {
            ++nonFinite;
            continue;
        }
        const double v = static_cast<double>(sample);
        sumSquares += v * v;
        peak = std::max(peak, std::abs(v));
    }
    const size_t finiteCount = audio.size() > static_cast<size_t>(nonFinite)
        ? audio.size() - static_cast<size_t>(nonFinite)
        : 0;
    metrics["rms"] = finiteCount > 0 ? std::sqrt(sumSquares / static_cast<double>(finiteCount)) : 0.0;
    metrics["peak"] = peak;
    metrics["nonFinite"] = nonFinite;
    return metrics;
}


QJsonObject p25VoiceRemoteDiagnosticsPayload(const Receiver& rx,
                                                    const P25VoiceAudioBlock& out,
                                                    qint64 nowMs)
{
    QJsonObject payload;
    payload["diag"] = p25VoiceDiagLabel(out.diag);
    payload["diagCode"] = static_cast<int>(out.diag);
    payload["talkgroup"] = static_cast<int>(out.talkgroupId ? out.talkgroupId : rx.p25VoiceTalkgroupId);
    payload["source"] = static_cast<int>(rx.p25VoiceSourceId);
    payload["deviceIndex"] = boundedJsonInt(rx.deviceIndex);
    payload["mode"] = QString::fromStdString(modeToString(rx.mode));
    payload["freqHz"] = rx.freqHz;
    payload["centerFreqHz"] = out.centerFreqHz;
    payload["effectiveTargetFreqHz"] = out.effectiveTargetFreqHz;
    payload["channelBwHz"] = rx.channelBwHz;
    payload["lpfHz"] = rx.lpfHz;
    payload["audioLpfEnabled"] = rx.audioLpfEnabled;
    payload["squelchDb"] = rx.squelchDb;
    payload["rfGainDb"] = rx.rfGainDb;
    payload["audioGain"] = rx.audioGain;
    payload["afcEnabled"] = rx.afcEnabled;
    payload["afcLocked"] = rx.afcLocked;
    payload["afcOffsetHz"] = rx.afcOffsetHz;
    payload["p25AfcFrozen"] = rx.p25AfcFrozen;
    payload["p25FrozenAfcOffsetHz"] = rx.p25FrozenAfcOffsetHz;

    QJsonObject traffic;
    traffic["voiceDecodeEnabled"] = rx.p25VoiceDecodeEnabled;
    traffic["controlMute"] = rx.p25ControlChannelMute;
    traffic["phase2"] = rx.p25VoicePhase2;
    traffic["clearKnown"] = rx.p25VoiceClearKnown;
    traffic["encrypted"] = rx.p25VoiceEncrypted;
    traffic["trafficRetunesPrimary"] = rx.p25TrafficRetunesPrimary;
    traffic["independentTrafficSource"] = rx.p25IndependentTrafficSource;
    traffic["controlFreqHz"] = rx.p25TrafficControlFreqHz;
    traffic["voiceFreqHz"] = rx.p25TrafficVoiceFreqHz;
    traffic["grantAgeMs"] = rx.p25TrafficLastGrantMs > 0
        ? static_cast<int>(std::clamp<qint64>(nowMs - rx.p25TrafficLastGrantMs, 0, std::numeric_limits<int>::max()))
        : -1;
    traffic["grantEpochMs"] = QString::number(rx.p25VoiceGrantEpochMs);
    traffic["sessionId"] = QString::number(rx.p25CurrentCallSessionId);
    traffic["generation"] = QString::number(rx.p25TrafficGeneration);
    payload["traffic"] = traffic;

    QJsonObject slot;
    slot["known"] = rx.p25VoiceTdmaSlotKnown;
    slot["slot"] = static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u);
    slot["trafficSlot"] = static_cast<int>(rx.p25TrafficSlot & 0x01u);
    slot["stickyInvert"] = rx.p25Phase2StickySlotLabelInvert;
    slot["oppositeOnlyWindows"] = rx.p25Phase2OppositeOnlyWindows;
    slot["grantedSlotImmutable"] = rx.p25Phase2GrantedSlotImmutable;
    slot["probePending"] = rx.p25VoiceSlotProbePending;
    slot["probeRequested"] = static_cast<int>(rx.p25VoiceSlotProbeRequested & 0x01u);
    payload["slot"] = slot;

    QJsonObject continuity;
    continuity["slotChanged"] = static_cast<qint64>(rx.p25DiagSlotChanged);
    continuity["stickyInvert"] = static_cast<qint64>(rx.p25DiagStickyInvert);
    continuity["slotProbe"] = static_cast<qint64>(rx.p25DiagSlotProbe);
    continuity["slotProbeBlocked"] = static_cast<qint64>(rx.p25DiagSlotProbeBlocked);
    continuity["securityChanged"] = static_cast<qint64>(rx.p25DiagSecurityChanged);
    continuity["securityLatch"] = static_cast<int>(rx.p25SessionState.callSecurityLatch);
    continuity["vocoderReset"] = static_cast<qint64>(rx.p25DiagVocoderReset);
    continuity["pendingAudioCleared"] = static_cast<qint64>(rx.p25DiagPendingAudioCleared);
    continuity["variantChanged"] = static_cast<qint64>(rx.p25DiagVariantChanged);
    continuity["ringUnderrun"] = static_cast<qint64>(rx.p25DiagRingUnderrun);
    continuity["ringOverflow"] = static_cast<qint64>(rx.p25DiagRingOverflow);
    continuity["sequencerGapSilence"] = static_cast<qint64>(rx.p25DiagSequencerGapSilence);
    continuity["sequencerLateDrops"] = static_cast<qint64>(rx.p25DiagSequencerLateDrops);
    continuity["sequencerNextOrdinal"] =
        static_cast<qint64>(rx.p25SessionState.frameSequencer.nextSpeechOrdinal);
    payload["continuity"] = continuity;

    QJsonObject mask;
    mask["paramsKnown"] = rx.p25VoiceMaskParamsKnown;
    mask["nac"] = QString("0x%1").arg(static_cast<uint>(rx.p25VoiceNac), 3, 16, QLatin1Char('0')).toUpper();
    mask["wacn"] = QString("0x%1").arg(static_cast<uint>(rx.p25VoiceWacn), 5, 16, QLatin1Char('0')).toUpper();
    mask["systemId"] = QString("0x%1").arg(static_cast<uint>(rx.p25VoiceSystemId), 3, 16, QLatin1Char('0')).toUpper();
    mask["targetOffsetKnown"] = rx.p25Phase2TrafficTargetOffsetKnown;
    mask["targetOffsetHz"] = rx.p25Phase2TrafficTargetOffsetHz;
    mask["targetOffsetTrust"] = rx.p25Phase2TrafficTargetOffsetTrust;
    mask["targetOffsetMisses"] = rx.p25Phase2TrafficTargetOffsetMisses;
    payload["mask"] = mask;

    QJsonObject voice;
    voice["syncs"] = boundedJsonInt(out.syncs);
    voice["nids"] = boundedJsonInt(out.nids);
    voice["nidLock"] = out.nidLock;
    voice["imbeFrames"] = boundedJsonInt(out.imbeFrames);
    voice["decodedFrames"] = boundedJsonInt(out.decodedFrames);
    voice["audioSamples"] = boundedJsonInt(out.audio.size());
    voice["backendAvailable"] = out.backendAvailable;
    voice["decoderRan"] = out.decoderRan;
    voice["demodPath"] = QString::fromStdString(out.demodPath).left(80);
    voice["audioMetrics"] = p25RemoteAudioMetrics(out.audio);
    payload["voice"] = voice;

    QJsonObject phase2;
    phase2["bursts"] = boundedJsonInt(out.phase2Bursts);
    phase2["voiceCodewords"] = boundedJsonInt(out.phase2VoiceCodewords);
    phase2["targetVoiceCodewords"] = boundedJsonInt(out.phase2TargetVoiceCodewords);
    phase2["oppositeVoiceCodewords"] = boundedJsonInt(out.phase2OppositeVoiceCodewords);
    phase2["slot0VoiceCodewords"] = boundedJsonInt(out.phase2Slot0VoiceCodewords);
    phase2["slot1VoiceCodewords"] = boundedJsonInt(out.phase2Slot1VoiceCodewords);
    phase2["slot0MacCrcValid"] = boundedJsonInt(out.phase2Slot0MacCrcValid);
    phase2["slot1MacCrcValid"] = boundedJsonInt(out.phase2Slot1MacCrcValid);
    phase2["oppositeAmbeAttempts"] = boundedJsonInt(out.phase2OppositeAmbeDecodeAttempts);
    phase2["oppositeAmbeAccepted"] = boundedJsonInt(out.phase2OppositeAmbeAcceptedFrames);
    phase2["oppositePendingQueued"] = boundedJsonInt(out.phase2OppositePendingQueued);
    phase2["oppositeRecordSamples"] = boundedJsonInt(out.phase2OppositeRecordSamples);
    phase2["expectedVoiceCodewords"] = boundedJsonInt(out.phase2ExpectedVoiceCodewords);
    phase2["freshStartAbsDibitKnown"] = out.phase2FreshStartAbsDibitKnown;
    phase2["freshStartAbsDibit"] = QString::number(out.phase2FreshStartAbsDibit);
    phase2["contextVoiceCodewords"] = boundedJsonInt(out.phase2ContextVoiceCodewords);
    phase2["contextSuppressedVoiceCodewords"] = boundedJsonInt(out.phase2ContextSuppressedVoiceCodewords);
    phase2["fedToMbelib"] = boundedJsonInt(out.phase2FedToMbelib);
    phase2["emittedPcmFrames"] = boundedJsonInt(out.phase2EmittedPcmFrames);
    phase2["emittedSpeechOrdinalFrames"] = boundedJsonInt(out.phase2EmittedSpeechOrdinals.size());
    phase2["concealmentFrames"] = boundedJsonInt(out.phase2ConcealmentFrames);
    phase2["feedGaps"] = boundedJsonInt(out.phase2FeedGaps);
    phase2["feedOrderIssues"] = boundedJsonInt(out.phase2FeedOrderIssues);
    phase2["firstFedAbsDibit"] = QString::number(out.phase2FirstFedAbsDibit);
    phase2["lastFedAbsDibit"] = QString::number(out.phase2LastFedAbsDibit);
    phase2["superframeBursts"] = boundedJsonInt(out.phase2SuperframeBursts);
    phase2["maskedBursts"] = boundedJsonInt(out.phase2MaskedBursts);
    phase2["targetMaskedBursts"] = boundedJsonInt(out.phase2TargetMaskedBursts);
    phase2["macPdus"] = boundedJsonInt(out.phase2MacPdus);
    phase2["macCrcValid"] = boundedJsonInt(out.phase2MacCrcValid);
    phase2["macFecDecoded"] = boundedJsonInt(out.phase2MacFecDecoded);
    phase2["macDirectCrcValid"] = boundedJsonInt(out.phase2MacDirectCrcValid);
    phase2["macRsDecoded"] = boundedJsonInt(out.phase2MacRsDecoded);
    phase2["acchStats"] = p25Phase2AcchStatsText(makeP25VoiceDiagnostics(out));
    phase2["ambeAttempts"] = boundedJsonInt(out.phase2AmbeDecodeAttempts);
    phase2["ambeAccepted"] = boundedJsonInt(out.phase2AmbeAcceptedFrames);
    phase2["ambeCanonical"] = boundedJsonInt(out.phase2AmbeAcceptedCanonicalFrames);
    phase2["ambeFallback"] = boundedJsonInt(out.phase2AmbeAcceptedFallbackFrames);
    phase2["diagnosticProbeAttempts"] = boundedJsonInt(out.phase2DiagnosticAmbeProbeAttempts);
    phase2["diagnosticProbeAccepted"] = boundedJsonInt(out.phase2DiagnosticAmbeProbeAccepted);
    phase2["duplicateSuppressed"] = boundedJsonInt(out.phase2DuplicateSuppressedVoiceCodewords);
    phase2["absoluteDuplicateSuppressed"] = boundedJsonInt(out.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
    phase2["sequencerSuppressed"] = boundedJsonInt(out.phase2SequencerSuppressedVoiceCodewords);
    phase2["rejectedVoiceCodewords"] = boundedJsonInt(out.phase2RejectedVoiceCodewords);
    phase2["inputQualityRejectedVoiceCodewords"] =
        boundedJsonInt(out.phase2InputQualityRejectedVoiceCodewords);
    phase2["wrongSlotVoiceCodewords"] = boundedJsonInt(out.phase2WrongSlotVoiceCodewords);
    phase2["trafficTalkgroupMismatchVoiceCodewords"] =
        boundedJsonInt(out.phase2TrafficTalkgroupMismatchVoiceCodewords);
    phase2["trafficTalkgroupStaleMismatchVoiceCodewords"] =
        boundedJsonInt(out.phase2TrafficTalkgroupStaleMismatchVoiceCodewords);
    payload["phase2"] = phase2;

    QJsonObject gates;
    gates["speakerGate"] = QString::fromStdString(out.phase2SpeakerGateReason.empty()
        ? p25VoiceBlockSpeakerGateReason(out)
        : out.phase2SpeakerGateReason).left(160);
    gates["securityAction"] = QString::fromStdString(out.phase2SecurityGateAction).left(160);
    gates["trustedClear"] = out.phase2SecurityTrustedClear;
    gates["trustedEncrypted"] = out.phase2SecurityTrustedEncrypted;
    gates["securityUnknown"] = out.phase2SecurityUnknown;
    gates["targetEssKnown"] = out.phase2TargetEssKnown;
    gates["targetEssEncrypted"] = out.phase2TargetEssEncrypted;
    gates["targetMacCrcValid"] = out.phase2TargetMacCrcValid;
    gates["targetSessionAudioRelease"] = out.phase2TargetSessionAudioRelease;
    gates["targetSecurityFromPtt"] = out.phase2TargetSecurityStateFromPtt;
    gates["currentFeedTrustedTargetBurst"] = out.phase2CurrentFeedTrustedTargetBurst;
    gates["sdrtrunkLateEntryRelease"] = out.phase2SdrtrunkLateEntryVoiceRelease;
    gates["explicitClearGrantRelease"] = out.phase2ExplicitClearGrantVoiceRelease;
    gates["skippedEncrypted"] = out.skippedEncrypted;
    gates["waitingForClearGrant"] = out.waitingForClearGrant;
    gates["audioLockMissing"] = out.phase2AudioLockMissing;
    gates["metadataMissing"] = out.phase2MetadataMissing;
    gates["maskMissing"] = out.phase2MaskMissing;
    gates["maskAppliedNoMacCrc"] = out.phase2MaskAppliedNoMacCrc;
    gates["essMissing"] = out.phase2EssMissing;
    gates["lateEntryWaiting"] = out.phase2LateEntryWaiting;
    gates["voiceUnsupported"] = out.phase2VoiceUnsupported;
    gates["ambeRejected"] = out.phase2AmbeRejected;
    gates["ambeVariantUnstable"] = out.phase2AmbeVariantUnstable;
    gates["tailGraceActive"] = out.phase2AudioTailGraceActive;
    gates["staleAudioTail"] = out.phase2StaleAudioTail;
    payload["gates"] = gates;
    payload["speakerGate"] = gates["speakerGate"];

    if (!out.decoderWarnings.empty()) {
        QJsonArray warnings;
        for (size_t i = 0; i < std::min<size_t>(out.decoderWarnings.size(), 4); ++i) {
            warnings.push_back(QString::fromStdString(out.decoderWarnings[i]).left(240));
        }
        payload["decoderWarnings"] = warnings;
    }
    return payload;
}


void publishP25VoiceDiagnostics(Receiver& rx, const P25VoiceAudioBlock& out, bool publishReceiver)
{
    if (!publishReceiver) return;
    QJsonObject remotePayload;
    bool shouldSubmitRemote = false;
    std::unique_lock<std::mutex> lk(rx.stateMutex);
    rx.p25VoiceDiagnostics = makeP25VoiceDiagnostics(out);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    rx.p25VoiceDiagnostics.updatedMs = static_cast<long long>(nowMs);
    if (rx.p25VoicePhase2) {
        p25Phase2NoteCadenceWindow(out);
    }
    if (rx.p25VoicePhase2 &&
        (out.phase2Bursts > 0 ||
         out.phase2VoiceCodewords > 0 ||
         out.phase2TargetVoiceCodewords > 0 ||
         out.phase2SuperframeBursts > 0 ||
         out.phase2MaskedBursts > 0 ||
         out.phase2MacPdus > 0 ||
         out.decodedFrames > 0 ||
         !out.audio.empty())) {
        rx.p25Phase2RecentTrafficEvidenceMs = static_cast<int64_t>(nowMs);
    }
    if (rx.p25VoicePhase2) {
        p25Phase2UpdateSessionSustainState(rx, out, nowMs, false);
        if (p25Phase2TargetHardClearEvidence(out) ||
            out.phase2TargetMacCrcValid ||
            out.decodedFrames > 0 ||
            !out.audio.empty()) {
            rx.p25Phase2MacEssStarveWindows = 0;
            rx.p25Phase2WideReacquireHoldWindows = 0;
            rx.p25Phase2ForceMaskEpochRehunt = false;
            rx.p25Phase2MaskEpochRepairHoldWindows = 0;
        } else if (p25Phase2MacEssStarvedVoiceWindow(out)) {
            rx.p25Phase2MacEssStarveWindows =
                std::min(rx.p25Phase2MacEssStarveWindows + 1, 1000);
            if (rx.p25Phase2MacEssStarveWindows >= 2) {
                // Capture 20260729_114627: SF/mask/targetVcw with p2mac=0/N was
                // treated as "wide reacquire" and hard-reset CQPSK, then fed 50 ms
                // crumbs → hundreds of p2bursts=0 windows and ~10 s audio / 504 s.
                // Soft-repair the sticky XOR/epoch only; keep the demod eye.
                rx.p25Phase2ForceMaskEpochRehunt = true;
                rx.p25Phase2MaskEpochRepairHoldWindows =
                    std::max(rx.p25Phase2MaskEpochRepairHoldWindows, 3);
            }
        } else if (rx.p25SessionState.sustain.hadSuccessfulEmit &&
                   out.phase2Bursts == 0 &&
                   out.phase2MaskedBursts == 0 &&
                   out.phase2TargetVoiceCodewords == 0 &&
                   out.decodedFrames == 0 &&
                   out.audio.empty()) {
            // DEC-0032 / capture 20260909_081701: raising MaskEpochRepairHoldWindows
            // on every post-emit empty eye stole DEC-0009 speaker-sustain
            // (planner skips sustain while maskEpochRepairWindow) and left the
            // call on `no voice sync`. Soft-rehunt sticky mask only; keep
            // 80+280 geometry.
            // DEC-0034 / capture 20260909_092250: also clearing m_blockCqpskHint
            // here wiped the only Costas continuity the block path has. After
            // one emit, two empty eyes → hint gone → permanent drop A
            // `no-vcw-from-live-window` while RF still had a 10 s eye earlier
            // in the same follow. Do not clearBlockCqpskHint on empty eyes.
            ++rx.p25Phase2PostEmitEmptyEyeWindows;
            if (rx.p25Phase2PostEmitEmptyEyeWindows >= 2) {
                rx.p25Phase2ForceMaskEpochRehunt = true;
                rx.p25Phase2PostEmitEmptyEyeWindows = 0;
            }
        } else if (out.phase2Bursts > 0 ||
                   out.phase2TargetVoiceCodewords > 0 ||
                   out.phase2FedToMbelib > 0 ||
                   out.decodedFrames > 0 ||
                   !out.audio.empty()) {
            rx.p25Phase2PostEmitEmptyEyeWindows = 0;
        } else if (out.phase2TargetVoiceCodewords == 0 &&
                   out.phase2ExpectedVoiceCodewords == 0 &&
                   out.phase2DiagnosticAmbeProbeAttempts == 0) {
            rx.p25Phase2MacEssStarveWindows = 0;
        }
    }

    // A queued slot-probe is only a hypothesis.  Field logs showed
    // slotProbePending=yes persisting for tens of seconds even while the granted
    // slot was producing valid target-slot AMBE/PCM.  If we later apply that stale
    // queued probe, the decoder flips away from a working slot and audio becomes
    // random/choppy.  Cancel pending probes as soon as the current slot proves
    // itself with real speaker-worthy target-slot audio.
    const bool currentSlotProducedUsefulAudio =
        rx.p25VoicePhase2 &&
        rx.p25VoiceSlotProbePending &&
        out.decodedFrames > 0 &&
        !out.audio.empty() &&
        out.phase2TargetVoiceCodewords > 0 &&
        out.phase2WrongSlotVoiceCodewords == 0 &&
        out.diag == P25VoiceDiagCode::Decoding;
    if (currentSlotProducedUsefulAudio) {
        rx.p25VoiceSlotProbePending = false;
        rx.p25VoiceSlotProbeRequested = 0;
    }
    publishP25VoiceDiagMirror(rx.p25VoiceDiagnostics,
        static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u),
        rx.p25VoiceTdmaSlotKnown);

    if (remoteDiagnosticsEnabled()) {
        shouldSubmitRemote =
            rx.p25VoiceDecodeEnabled ||
            rx.p25VoicePhase2 ||
            out.phase2Bursts > 0 ||
            out.phase2VoiceCodewords > 0 ||
            out.decodedFrames > 0 ||
            !out.audio.empty() ||
            out.waitingForClearGrant ||
            out.skippedEncrypted;
        if (shouldSubmitRemote) {
            remotePayload = p25VoiceRemoteDiagnosticsPayload(rx, out, nowMs);
        }
    }
    lk.unlock();

    if (shouldSubmitRemote) {
        remoteDiagnosticsSubmit("p25.voice", "debug", remotePayload);
    }
}


void clearP25VoiceDiagnostics(Receiver& rx)
{
    rx.p25VoiceDiagnostics = P25VoiceDiagSnapshot{};
    rx.p25Phase2RecentTrafficEvidenceMs = 0;
    publishP25VoiceDiagMirror(rx.p25VoiceDiagnostics, 0, false);
}


void clearP25VoiceFollowFieldsLocked(Receiver& rx, bool controlMute)
{
    rx.p25VoiceDecodeEnabled = false;
    rx.p25VoiceClearKnown = false;
    rx.p25VoiceEncrypted = false;
    // pending cleared by caller or publish path
    rx.p25VoiceTalkgroupId = 0;
    rx.p25VoicePhase2 = false;
    rx.p25VoiceTdmaSlotKnown = false;
    rx.p25VoiceTdmaSlot = 0;
    rx.p25VoiceSlotProbePending = false;
    rx.p25VoiceSlotProbeRequested = 0;
    rx.p25VoiceMaskParamsKnown = false;
    rx.p25VoiceNac = 0;
    rx.p25VoiceWacn = 0;
    rx.p25VoiceSystemId = 0;
    rx.p25VoiceSettleUntilMs = 0;
    rx.p25VoiceDiscardWindows = 0;
    rx.p25Phase2RecentTrafficEvidenceMs = 0;
    rx.p25ControlChannelMute = controlMute;
    rx.p25AfcFrozen = false;
    rx.p25FrozenAfcOffsetHz = 0.0;
    rx.p25IndependentTrafficSource = false;
    rx.p25TrafficRetunesPrimary = false;
    rx.p25TrafficGeneration = 0;
    rx.p25TrafficControlFreqHz = 0.0;
    rx.p25TrafficSourceCenterFreqHz = 0.0;
    rx.p25TrafficVoiceFreqHz = 0.0;
    rx.p25TrafficSlot = 0;
    rx.p25TrafficLastGrantMs = 0;
    rx.p25Phase2TrafficTargetOffsetKnown = false;
    rx.p25Phase2TrafficTargetOffsetHz = 0.0;
    rx.p25Phase2TrafficTargetOffsetTrust = 0;
    rx.p25Phase2TrafficTargetOffsetMisses = 0;
    rx.p25Phase2AllowLateEntryAudioProbe = kP25Phase2AllowUnknownGrantFieldAudioProbe;
    p25ClearPhase2PendingAudio(rx);
    rx.p25VoiceResetPending = true;
    clearP25VoiceDiagnostics(rx);
}


bool tryApplyP25VoiceResetLocked(Receiver& rx)
{
    std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
    if (!dspLock.owns_lock()) return false;
    const bool preserveFollow = rx.p25VoiceDecodeEnabled;
    const bool voiceDecodeEnabled = rx.p25VoiceDecodeEnabled;
    const bool voiceClearKnown = rx.p25VoiceClearKnown;
    const bool voiceEncrypted = rx.p25VoiceEncrypted;
    const uint32_t talkgroupId = rx.p25VoiceTalkgroupId;
    const uint32_t sourceId = rx.p25VoiceSourceId;
    const int64_t grantEpochMs = rx.p25VoiceGrantEpochMs;
    const uint64_t currentCallSessionId = rx.p25CurrentCallSessionId;
    const uint64_t pttGeneration = rx.p25PttGeneration;
    const bool phase2 = rx.p25VoicePhase2;
    const bool slotKnown = rx.p25VoiceTdmaSlotKnown;
    const uint8_t slot = rx.p25VoiceTdmaSlot;
    const bool grantedSlotImmutable = rx.p25Phase2GrantedSlotImmutable;
    const bool maskKnown = rx.p25VoiceMaskParamsKnown;
    const uint16_t nac = rx.p25VoiceNac;
    const uint32_t wacn = rx.p25VoiceWacn;
    const uint16_t systemId = rx.p25VoiceSystemId;
    const int64_t settleUntilMs = rx.p25VoiceSettleUntilMs;
    const int discardWindows = rx.p25VoiceDiscardWindows;
    const bool allowLateEntryProbe = rx.p25Phase2AllowLateEntryAudioProbe;
    const bool targetOffsetKnown = rx.p25Phase2TrafficTargetOffsetKnown;
    const double targetOffsetHz = rx.p25Phase2TrafficTargetOffsetHz;
    const int targetOffsetTrust = rx.p25Phase2TrafficTargetOffsetTrust;
    const int targetOffsetMisses = rx.p25Phase2TrafficTargetOffsetMisses;
    p25ClearPhase2PendingAudio(rx);
    rx.resetP25VoiceState();
    if (preserveFollow) {
        rx.p25VoiceDecodeEnabled = voiceDecodeEnabled;
        rx.p25VoiceClearKnown = voiceClearKnown;
        rx.p25VoiceEncrypted = voiceEncrypted;
        rx.p25VoiceTalkgroupId = talkgroupId;
        rx.p25VoiceSourceId = sourceId;
        rx.p25VoiceGrantEpochMs = grantEpochMs;
        rx.p25CurrentCallSessionId = currentCallSessionId;
        rx.p25PttGeneration = pttGeneration;
        rx.p25VoicePhase2 = phase2;
        rx.p25VoiceTdmaSlotKnown = slotKnown;
        rx.p25VoiceTdmaSlot = slot;
        rx.p25Phase2GrantedSlotImmutable = grantedSlotImmutable;
        rx.p25VoiceMaskParamsKnown = maskKnown;
        rx.p25VoiceNac = nac;
        rx.p25VoiceWacn = wacn;
        rx.p25VoiceSystemId = systemId;
        rx.p25VoiceSettleUntilMs = settleUntilMs;
        rx.p25VoiceDiscardWindows = discardWindows;
        rx.p25Phase2AllowLateEntryAudioProbe = allowLateEntryProbe;
        rx.p25Phase2TrafficTargetOffsetKnown = targetOffsetKnown;
        rx.p25Phase2TrafficTargetOffsetHz = targetOffsetHz;
        rx.p25Phase2TrafficTargetOffsetTrust = targetOffsetTrust;
        rx.p25Phase2TrafficTargetOffsetMisses = targetOffsetMisses;
        rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));
        if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
            rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
        } else {
            rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
        }
    }
    rx.p25VoiceResetPending = false;
    clearP25VoiceDiagnostics(rx);
    return true;
}


void syncP25Phase2MaskParametersToLiveDecoder(Receiver& rx)
{
    if (!rx.p25VoicePhase2) {
        if (rx.p25VoiceLiveDecoder.phase2MaskParametersKnown()) {
            rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
        }
        return;
    }

    if (rx.p25VoiceMaskParamsKnown) {
        if (!rx.p25VoiceLiveDecoder.phase2MaskParametersMatch(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId)) {
            rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
        }
    } else if (rx.p25VoiceLiveDecoder.phase2MaskParametersKnown()) {
        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
    }
}


uint64_t p25TrafficProcessorSessionId(const Receiver& rx)
{
    return rx.p25CurrentCallSessionId != 0 ? rx.p25CurrentCallSessionId : 1;
}


P25TrafficChannelProcessor* ensureP25TrafficProcessor(Receiver& rx)
{
    if (!rx.p25VoicePhase2) {
        rx.p25TrafficProcessor.reset();
        return nullptr;
    }

    const uint64_t sessionId = p25TrafficProcessorSessionId(rx);
    const int grantedSlot = rx.p25VoiceTdmaSlotKnown
        ? static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u)
        : -1;
    const double voiceFreqHz = rx.p25TrafficVoiceFreqHz > 0.0
        ? rx.p25TrafficVoiceFreqHz
        : rx.freqHz;
    const uint32_t voiceHz = static_cast<uint32_t>(std::clamp(
        std::isfinite(voiceFreqHz) ? voiceFreqHz : 0.0,
        0.0,
        static_cast<double>(std::numeric_limits<uint32_t>::max())));

    if (!rx.p25TrafficProcessor ||
        rx.p25TrafficProcessor->getDiag().sessionId != sessionId) {
        rx.p25TrafficProcessor = std::make_unique<P25TrafficChannelProcessor>(
            sessionId, rx.p25VoiceTalkgroupId, voiceHz, grantedSlot);
    }

    if (rx.p25VoiceMaskParamsKnown) {
        rx.p25TrafficProcessor->setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
    } else {
        rx.p25TrafficProcessor->clearPhase2MaskParameters();
    }
    return rx.p25TrafficProcessor.get();
}


void p25UpdateTrafficProcessorFromLiveDecode(Receiver& rx,
                                                    const P25LiveDecodeResult& live,
                                                    uint64_t windowStartAbsDibit,
                                                    bool haveAbsoluteDibits)
{
    if (!rx.p25VoicePhase2) return;
    auto* trafficProcessor = ensureP25TrafficProcessor(rx);
    if (!trafficProcessor) return;

    const uint64_t dibitEnd = haveAbsoluteDibits
        ? (live.dibits.empty()
            ? windowStartAbsDibit
            : windowStartAbsDibit + static_cast<uint64_t>(live.dibits.size()))
        : 0;
    trafficProcessor->observeDecodeResult(live, dibitEnd);
}



P25TrafficProcessorStatusSnapshot snapshotP25TrafficProcessorStatus(const Receiver& rx)
{
    P25TrafficProcessorStatusSnapshot out;
    if (!rx.p25TrafficProcessor) return out;
    out.present = true;
    out.diag = rx.p25TrafficProcessor->getDiag();
    out.callActive = rx.p25TrafficProcessor->isCallStillActive();
    return out;
}



static P25FollowGuiStatusCache gP25FollowGuiStatusCache;
static std::mutex gP25FollowGuiStatusCacheMutex;

void updateP25FollowGuiStatusCache(const P25FollowGuiStatusCache& snapshot)
{
    std::lock_guard<std::mutex> lk(gP25FollowGuiStatusCacheMutex);
    gP25FollowGuiStatusCache = snapshot;
}


bool loadP25FollowGuiStatusCache(P25FollowGuiStatusCache& out, qint64 maxAgeMs)
{
    std::lock_guard<std::mutex> lk(gP25FollowGuiStatusCacheMutex);
    if (gP25FollowGuiStatusCache.updatedMs <= 0) return false;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (maxAgeMs > 0 && nowMs - gP25FollowGuiStatusCache.updatedMs > maxAgeMs) return false;
    out = gP25FollowGuiStatusCache;
    return true;
}


bool p25FollowGuiStatusCacheMatchesActiveFollow(const P25FollowGuiStatusCache& cached,
                                                       bool independentTrafficActive,
                                                       uint32_t followTalkgroupId,
                                                       double followVoiceHz,
                                                       uint64_t trafficGeneration) noexcept
{
    if (!independentTrafficActive) return true;
    if (!cached.independentTrafficSource) return false;
    if (trafficGeneration != 0 &&
        cached.trafficGeneration != 0 &&
        cached.trafficGeneration != trafficGeneration) {
        return false;
    }
    if (followVoiceHz > 0.0 &&
        cached.trafficVoiceFreqHz > 0.0 &&
        std::abs(cached.trafficVoiceFreqHz - followVoiceHz) > 50.0) {
        return false;
    }
    if (followTalkgroupId != 0 &&
        cached.voiceDiag.talkgroupId != 0 &&
        cached.voiceDiag.talkgroupId != followTalkgroupId) {
        return false;
    }
    return cached.voiceStateDecodeEnabled || cached.trafficStatus.present ||
        cached.voiceDiag.phase2Bursts > 0 || cached.voiceDiag.phase2VoiceCodewords > 0;
}


void p25Phase2ResetPlayoutBridge(Receiver& rx) noexcept;
bool p25Phase2PromoteCompanionModules(Receiver& rx, const char* why) noexcept;

void p25CommitPhase2TrafficMetadataFollow(Receiver& rx,
                                                 const P25TalkgroupEntry& followTg,
                                                 double ccHz,
                                                 qint64 nowMs)
{
    const double priorVoiceHz = p25Phase2VoiceSchedulerNominalHz(rx);
    const bool slotCompatible =
        !rx.p25VoiceTdmaSlotKnown ||
        !followTg.tdmaSlotKnown ||
        static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) ==
            static_cast<uint8_t>(followTg.tdmaSlot & 0x01u);
    const bool sameCall =
        rx.p25VoiceDecodeEnabled &&
        rx.p25VoicePhase2 &&
        rx.p25CurrentCallSessionId != 0 &&
        rx.p25VoiceTalkgroupId == followTg.talkgroupId &&
        slotCompatible &&
        followTg.lastVoiceFreqHz > 0.0 &&
        std::isfinite(followTg.lastVoiceFreqHz) &&
        priorVoiceHz > 0.0 &&
        std::isfinite(priorVoiceHz) &&
        std::abs(priorVoiceHz - followTg.lastVoiceFreqHz) <= 50.0;
    const bool sameAllocationControlSourceChange =
        sameCall &&
        followTg.lastSourceId != 0 &&
        rx.p25VoiceSourceId != 0 &&
        rx.p25VoiceSourceId != followTg.lastSourceId;

    const bool slotFlipSameRf =
        rx.p25VoiceTdmaSlotKnown &&
        followTg.tdmaSlotKnown &&
        static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) !=
            static_cast<uint8_t>(followTg.tdmaSlot & 0x01u) &&
        followTg.lastVoiceFreqHz > 0.0 &&
        std::isfinite(followTg.lastVoiceFreqHz) &&
        priorVoiceHz > 0.0 &&
        std::isfinite(priorVoiceHz) &&
        std::abs(priorVoiceHz - followTg.lastVoiceFreqHz) <= 50.0;
    const bool companionMatchesIncoming =
        rx.p25SessionState.pendingAudioOpposite.armed &&
        followTg.tdmaSlotKnown &&
        rx.p25SessionState.pendingAudioOpposite.key.slot ==
            static_cast<uint8_t>(followTg.tdmaSlot & 0x01u) &&
        (rx.p25SessionState.pendingAudioOpposite.key.talkgroupId == followTg.talkgroupId ||
         (rx.p25SessionState.pendingAudioOpposite.key.talkgroupId & 0x7F000000u) == 0x7F000000u);
    auto stampIncomingCallIdentityForNewPtt = [&]() noexcept {
        rx.p25VoiceTalkgroupId = followTg.talkgroupId;
        if (followTg.lastSourceId != 0) {
            p25Phase2AdoptGrantSourceIdForCurrentCall(rx, followTg.lastSourceId);
        }
        rx.p25VoiceTdmaSlotKnown = followTg.tdmaSlotKnown;
        rx.p25VoiceTdmaSlot = followTg.tdmaSlot;
        if (followTg.lastVoiceFreqHz > 0.0 && std::isfinite(followTg.lastVoiceFreqHz)) {
            rx.freqHz = followTg.lastVoiceFreqHz;
        }
        rx.p25TrafficVoiceFreqHz = followTg.lastVoiceFreqHz;
        rx.p25TrafficSlot = followTg.tdmaSlotKnown ? static_cast<uint8_t>(followTg.tdmaSlot & 0x01u) : 0;
        rx.p25TrafficControlFreqHz = ccHz;
        rx.p25TrafficLastGrantMs = nowMs;
    };

    if (slotFlipSameRf && companionMatchesIncoming) {
        p25Phase2PromoteCompanionModules(rx, "metadata-same-rf");
        rx.p25SessionState.pendingAudioOpposite = {};
        rx.p25SessionState.ambeDedupe = {};
        rx.p25SessionState.audioTail = {};
        if (!sameCall) {
            // p25Phase2BeginNewPtt derives the call-session key from the
            // receiver's current TG. Stamp the incoming grant first or accepted
            // speech can be keyed to TG 0 and starve/de-dupe live playout.
            stampIncomingCallIdentityForNewPtt();
            p25Phase2BeginNewPtt(rx, nowMs);
            rx.p25Phase2GrantedSlotImmutable = false;
            rx.p25SessionState.sustain = {};
            rx.p25SessionState.callSecurityLatch = P25CallSecurityLatch::Unknown;
            p25ClearPhase2RecentSecurityEvidence(rx);
        }
    } else if (!sameCall) {
        p25ClearPhase2PendingAudio(rx);
        rx.p25SessionState.ambeDedupe = {};
        rx.p25SessionState.audioTail = {};
        stampIncomingCallIdentityForNewPtt();
        p25Phase2BeginNewPtt(rx, nowMs);
        rx.p25Phase2GrantedSlotImmutable = false;
    }
    rx.p25VoiceDecodeEnabled = true;
    rx.p25VoicePhase2 = true;
    // Same-call OP=0x02 unknown updates must not wipe traffic-proven clear
    // (mirrors same-call in-place follow at ~19907). Capture 20260909_053448:
    // re-arm paths that still hit metadata commit after clearTrusted would
    // otherwise force unknownProbe again and cold mega-eyes.
    if (p25TalkgroupGrantProvesSpeakerEncrypted(followTg)) {
        rx.p25VoiceClearKnown = false;
        rx.p25VoiceEncrypted = true;
    } else if (p25TalkgroupGrantProvesSpeakerClear(followTg)) {
        rx.p25VoiceClearKnown = true;
        rx.p25VoiceEncrypted = false;
    } else if (!(sameCall &&
                 rx.p25VoiceClearKnown &&
                 !rx.p25VoiceEncrypted)) {
        rx.p25VoiceClearKnown = false;
        rx.p25VoiceEncrypted = false;
    }
    rx.p25VoiceTalkgroupId = followTg.talkgroupId;
    if (followTg.lastSourceId != 0 &&
        (!sameAllocationControlSourceChange || rx.p25VoiceSourceId == 0)) {
        p25Phase2AdoptGrantSourceIdForCurrentCall(rx, followTg.lastSourceId);
    }
    rx.p25VoiceTdmaSlotKnown = followTg.tdmaSlotKnown;
    rx.p25VoiceTdmaSlot = followTg.tdmaSlot;
    if (followTg.tdmaSlotKnown) {
        p25Phase2MarkGrantedSlotImmutable(rx);
    }
    if (followTg.lastVoiceFreqHz > 0.0 && std::isfinite(followTg.lastVoiceFreqHz)) {
        rx.freqHz = followTg.lastVoiceFreqHz;
    }
    rx.p25TrafficVoiceFreqHz = followTg.lastVoiceFreqHz;
    rx.p25TrafficSlot = followTg.tdmaSlotKnown ? static_cast<uint8_t>(followTg.tdmaSlot & 0x01u) : 0;
    rx.p25TrafficControlFreqHz = ccHz;
    rx.p25TrafficLastGrantMs = nowMs;
    if (followTg.p25MaskParamsKnown) {
        rx.p25VoiceMaskParamsKnown = true;
        rx.p25VoiceNac = followTg.nac;
        rx.p25VoiceWacn = followTg.wacn;
        rx.p25VoiceSystemId = followTg.systemId;
    }
    rx.p25VoiceSettleUntilMs = std::min<qint64>(rx.p25VoiceSettleUntilMs, nowMs + 80);
    rx.p25VoiceDiscardWindows = 0;
    rx.p25VoiceSlotProbePending = false;
    rx.p25VoiceSlotProbeRequested = 0;
    const bool carrierChanged =
        rx.p25IndependentTrafficSource &&
        followTg.lastVoiceFreqHz > 0.0 &&
        std::isfinite(followTg.lastVoiceFreqHz) &&
        priorVoiceHz > 0.0 &&
        std::isfinite(priorVoiceHz) &&
        std::abs(priorVoiceHz - followTg.lastVoiceFreqHz) > 50.0;
    if (carrierChanged) {
        p25Phase2ResetTrafficTargetOffset(rx);
        rx.p25VoiceSettleUntilMs = nowMs + 80;
        rx.p25VoiceDiscardWindows = 0;
        rx.p25VoiceResetPending = true;
    }
    std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
    if (dspLock.owns_lock() && rx.p25VoiceMaskParamsKnown) {
        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
    }
    if (dspLock.owns_lock() && followTg.tdmaSlotKnown) {
        rx.p25VoiceLiveDecoder.setPhase2PreferredTdmaSlot(
            true, static_cast<uint8_t>(followTg.tdmaSlot & 0x01u));
    }
}


bool p25Phase2ShouldFreezeCqpskDiscrete(const Receiver& rx) noexcept
{
    // Never freeze. Capture 20260807_235726: streaming DDC + freeze-after-emit
    // locked a dead Gardner eye (emit=7, empty=805, ~45k underruns). SDRTrunk's
    // Costas loop keeps tracking; it does not freeze the NCO after the first
    // Voice4. Search is already bounded by the hot realtime budget.
    (void)rx;
    return false;
}


void p25Phase2ResetPlayoutBridge(Receiver& rx) noexcept;

// Promote companion TDMA AudioModule to selected speaker path by swapping
// AMBE + pending + resampler state. Never mixes PCM streams.
bool p25Phase2PromoteCompanionModules(Receiver& rx, const char* why) noexcept
{
    P25AmbeVoiceDecoder movedSelected = std::move(rx.p25AmbeVoiceDecoder);
    rx.p25AmbeVoiceDecoder = std::move(rx.p25AmbeVoiceDecoderOpposite);
    rx.p25AmbeVoiceDecoderOpposite = std::move(movedSelected);
    std::swap(rx.p25SessionState.pendingAudio, rx.p25SessionState.pendingAudioOpposite);
    std::swap(rx.p25SessionState.resampler, rx.p25SessionState.resamplerOpposite);
    rx.p25Phase2LastGoodPcm.clear();
    rx.p25SessionState.frameSequencer = {};
    ++rx.p25DiagCompanionPromoted;
    if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
        spdlog::info("P25 COMPANION_PROMOTED why={} tg={} slot={} pendingTg={} pendingSlot={}",
                     why ? why : "?",
                     rx.p25VoiceTalkgroupId,
                     static_cast<unsigned>(rx.p25VoiceTdmaSlot & 0x01u),
                     rx.p25SessionState.pendingAudio.key.talkgroupId,
                     static_cast<unsigned>(rx.p25SessionState.pendingAudio.key.slot));
    }
    return true;
}


bool applyP25Phase2SlotProbeLocked(Receiver& rx, uint8_t newSlot, qint64 nowMs)
{
    // Architectural rule (SDRTrunk P25P2AudioModule): once the grant slot is
    // locked for the current call, never flip slot / reset decoder / clear IQ
    // pending mid-call.  Slot probing is acquisition-only.
    const uint8_t requested = static_cast<uint8_t>(newSlot & 0x01u);
    if (p25Phase2GrantedSlotIsImmutable(rx)) {
        ++rx.p25DiagSlotProbeBlocked;
        rx.p25VoiceSlotProbePending = false;
        rx.p25VoiceSlotProbeRequested = 0;
        rx.p25Phase2StickySlotLabelInvert = false;
        if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
            spdlog::info("P25 SLOT_PROBE blocked (immutable grant slot) tg={} slot={} req={}",
                         rx.p25VoiceTalkgroupId,
                         static_cast<unsigned>(rx.p25VoiceTdmaSlot & 0x01u),
                         static_cast<unsigned>(requested));
        }
        return false;
    }

    // Non-destructive acquisition probe: retarget the granted TDMA slot and
    // reset receiver-side audio/sequencer state, but keep the live decoder,
    // CQPSK lock, and per-slot Phase-2 sessions intact (SDRTrunk parity).
    const uint8_t oldSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
    ++rx.p25DiagSlotProbe;
    if (rx.p25VoiceTdmaSlotKnown && oldSlot != requested) {
        ++rx.p25DiagSlotChanged;
        if (p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled()) {
            spdlog::warn("P25 SLOT_CHANGED {}->{} tg={} (acquisition probe)",
                         static_cast<unsigned>(oldSlot),
                         static_cast<unsigned>(requested),
                         rx.p25VoiceTalkgroupId);
        }
    }

    p25ClearPhase2RecentSecurityEvidence(rx);
    rx.p25SessionState.frameSequencer = {};
    rx.p25SessionState.ambeDedupe = {};
    p25Phase2ResetPlayoutBridge(rx);
    // Slot selection change: promote companion AMBE/pending to selected (swap),
    // then drop the demoted (old selected) pending. Never merge PCM streams.
    if (rx.p25VoiceTdmaSlotKnown && oldSlot != requested) {
        p25Phase2PromoteCompanionModules(rx, "slot-probe");
        rx.p25SessionState.pendingAudioOpposite = {};
        rx.p25Phase2PendingAudio.clear();
        rx.p25Phase2PendingTalkgroupId = 0;
        rx.p25Phase2PendingAudioArmed = false;
        if (rx.p25SessionState.pendingAudio.armed) {
            rx.p25Phase2PendingTalkgroupId = rx.p25SessionState.pendingAudio.key.talkgroupId;
            rx.p25Phase2PendingAudioArmed = true;
            rx.p25Phase2PendingAudio.assign(
                rx.p25SessionState.pendingAudio.ambeFrames.size() * 960u, 0.0f);
        }
    } else {
        p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::SlotProbeDestructive);
    }
    rx.p25Phase2OppositeOnlyWindows = 0;
    rx.p25Phase2StickySlotLabelInvert = false;

    rx.p25VoiceTdmaSlotKnown = true;
    rx.p25VoiceTdmaSlot = requested;
    rx.p25TrafficSlot = requested;
    rx.p25VoiceSlotProbePending = false;
    rx.p25VoiceSlotProbeRequested = 0;
    rx.p25VoiceSettleUntilMs = nowMs + kP25Phase2SlotProbeSettleMs;
    rx.p25VoiceDiscardWindows = std::max<int>(rx.p25VoiceDiscardWindows, kP25Phase2SlotProbeDiscardWindows);
    rx.p25ControlChannelMute = false;
    rx.p25VoiceLiveDecoder.setPhase2PreferredTdmaSlot(true, requested);

    const bool maskKnown = rx.p25VoiceMaskParamsKnown;
    const bool clearKnown = rx.p25VoiceClearKnown;
    const bool encrypted = rx.p25VoiceEncrypted;
    if (maskKnown || clearKnown || encrypted) {
        p25Phase2MarkGrantedSlotImmutable(rx);
    }
    return true;
}


void pushAudioFrames(AudioEngine* engine,
                            std::vector<float>& pending,
                            const std::vector<float>& audio,
                            const std::vector<size_t>& activeOutputIndices,
                            size_t frameSize,
                            size_t maxPendingSamples)
{
    if (!engine || audio.empty() || frameSize == 0) return;

    pending.insert(pending.end(), audio.begin(), audio.end());

    size_t consumed = 0;
    while (pending.size() - consumed >= frameSize) {
        engine->pushAudioToActiveOutputs(pending.data() + consumed, frameSize, activeOutputIndices);
        consumed += frameSize;
    }
    if (consumed > 0) {
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(consumed));
    }

    constexpr size_t kKeepPendingFrames = 4;
    const size_t keepPendingSamples = frameSize * kKeepPendingFrames;
    if (pending.size() > maxPendingSamples) {
        pending.erase(pending.begin(), pending.end() - static_cast<std::ptrdiff_t>(keepPendingSamples));
    }
}



size_t pushP25LiveStreamingAudio(AudioEngine* engine,
                                        std::vector<float>& pending,
                                        const std::vector<float>& audio,
                                        const std::vector<size_t>& activeOutputIndices,
                                        size_t frameSize,
                                        double ringFillPercent,
                                        bool warmPendingRealAudio,
                                        std::vector<float>* pushedRealAudio,
                                        bool endOfStream)
{
    if (!engine || frameSize == 0) return 0;
    const bool hasFreshAudio = !audio.empty();
    if (!audio.empty()) {
        pending.insert(pending.end(), audio.begin(), audio.end());
    }
    if (pending.size() < frameSize) return 0;

    const double outRate = std::max(8000.0, static_cast<double>(engine->getSampleRate()));
    const size_t jitterCap = engine->getJitterQueueCapFrames();
    // Cold starts need enough selected-slot PCM to survive the measured live
    // Phase-2 worker cadence.  Capture 20260916_092651 showed the healthy
    // 240+280 ms traffic jobs producing valid PCM, but p50 DSP was ~553 ms
    // while the old 120-180 ms prime opened as one-word islands.  A bounded
    // 240-320 ms cushion is below the engine's 650 ms jitter cap and keeps the
    // call delayed rather than chopped.
    const size_t coldPrimeSamples = std::max(frameSize * 16,
        static_cast<size_t>(outRate * 0.320)); // 320 ms startup cushion
    const size_t hotPrimeSamples = std::max(frameSize * 12,
        static_cast<size_t>(outRate * 0.240)); // 240 ms mid-call restart cushion
    // Use sample-accurate ring depth for pacing.  ringFillPercent is UI/log
    // telemetry and can be stale relative to the realtime audio callback; using
    // it as producer flow control under-pushed accepted Phase-2 speech when the
    // reported fill looked high but the fresh block still needed to be queued.
    (void)ringFillPercent;
    size_t queuedNow = engine->getRingQueuedSamples();
    const bool realAudioReady = hasFreshAudio || warmPendingRealAudio;
    const bool warmPlaybackContext = realAudioReady && (queuedNow > 0 || warmPendingRealAudio);
    const bool midCallRingRestart =
        realAudioReady && queuedNow == 0 && pending.size() >= hotPrimeSamples;
    const size_t minPrimeSamples =
        (midCallRingRestart || warmPlaybackContext) ? hotPrimeSamples : coldPrimeSamples;
    const size_t jitterSoftCap = (jitterCap > frameSize * 4)
        ? std::max(coldPrimeSamples, (jitterCap * 7) / 8)
        : std::max(coldPrimeSamples, static_cast<size_t>(outRate * 0.480));
    // Hold a deeper real selected-slot cushion so opposite-slot dwell and
    // 500-600 ms worker holes cannot drain the ring to underrun between hops.
    const size_t targetQueuedSamples = std::min(jitterSoftCap, std::max(minPrimeSamples,
        static_cast<size_t>(outRate * 0.420)));
    // Validated selected-slot PCM must outrank the clock bridge, but live
    // Phase-2 speech should not fill the entire engine jitter cap.  Capture
    // 20260916_101342 showed clear decoded frames arriving while the ring was
    // already 98-100% full; that delayed fresh speech behind older call
    // context and surfaced as one clear talker with other emits blank/garbled.
    // Keep about a half-second of live cushion and leave deterministic
    // headroom for the next selected-slot AMBE frame group.
    const size_t measuredCadenceCeiling = std::max(targetQueuedSamples,
        static_cast<size_t>(outRate * 0.480));
    const size_t pushCeilingSamples = jitterCap > 0
        ? std::min(jitterCap, measuredCadenceCeiling)
        : measuredCadenceCeiling;
    const size_t ringLowWaterSamples = std::max(frameSize * 12,
        static_cast<size_t>(outRate * 0.240));
    const size_t maxPendingSamples = std::max(pushCeilingSamples,
        static_cast<size_t>(outRate * 0.650)); // bounded producer-side stash
    if (realAudioReady) {
        const size_t playablePending = (pending.size() / frameSize) * frameSize;
        const size_t desiredRealAhead = std::min(playablePending, pushCeilingSamples);
        if (desiredRealAhead >= frameSize && queuedNow + desiredRealAhead > pushCeilingSamples) {
            // Clock bridge is allowed to hide underruns, but it must be
            // preemptable. Otherwise bridge silence fills the queue cap and
            // the next real AMBE island gets under-pushed/played late.
            const size_t bridgeHeadroomDeficit =
                queuedNow + desiredRealAhead - pushCeilingSamples;
            const size_t droppedBridge =
                engine->dropQueuedBridgeAudio(bridgeHeadroomDeficit, activeOutputIndices);
            if (droppedBridge > 0) {
                queuedNow = engine->getRingQueuedSamples();
            }
        }
    }
    // SDRTrunk AudioChannel pulls 160 samples / 20 ms. Batching 80 ms here
    // re-chunked a live stream that already arrived as 20 ms AMBE frames.
    const size_t minFreshPushSamples = frameSize;

    if (p25SpeakerNeedsStartupPrime(queuedNow, pending.size(), minPrimeSamples, endOfStream)) {
        if (pending.size() > maxPendingSamples) {
            pending.erase(pending.begin(),
                pending.end() - static_cast<std::ptrdiff_t>(maxPendingSamples));
        }
        return 0;
    }

    size_t totalPushed = 0;
    while (pending.size() - totalPushed >= frameSize) {
        queuedNow = engine->getRingQueuedSamples();
        const size_t queueLimit = realAudioReady ? pushCeilingSamples : targetQueuedSamples;
        if (queuedNow >= queueLimit) break;

        const size_t headroom = queueLimit - queuedNow;
        const size_t pendingRemaining = pending.size() - totalPushed;
        const size_t effectiveMinFresh = (queuedNow < ringLowWaterSamples)
            ? frameSize
            : minFreshPushSamples;
        const size_t minBatch = hasFreshAudio
            ? std::min(pendingRemaining, effectiveMinFresh)
            : frameSize;
        if (headroom < minBatch) break;

        size_t batch = std::min(pendingRemaining, headroom);
        batch = (batch / frameSize) * frameSize;
        if (batch < frameSize) break;

        if (pushedRealAudio) {
            pushedRealAudio->insert(pushedRealAudio->end(),
                                    pending.begin() + static_cast<std::ptrdiff_t>(totalPushed),
                                    pending.begin() + static_cast<std::ptrdiff_t>(totalPushed + batch));
        }
        engine->pushAudioToActiveOutputs(pending.data() + totalPushed, batch, activeOutputIndices);
        // DEC-0050: record what the speaker actually heard during start/stop IQ capture.
        appendLiveIqSpeakerWavCapture(pending.data() + totalPushed, batch);
        totalPushed += batch;
        queuedNow = engine->getRingQueuedSamples();
    }

    if (totalPushed > 0) {
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(totalPushed));
    }
    if (pending.size() > maxPendingSamples) {
        pending.erase(pending.begin(),
            pending.end() - static_cast<std::ptrdiff_t>(maxPendingSamples));
    }
    return totalPushed;
}


size_t pushP25SpeakerAudio(AudioEngine* engine,
                                  std::vector<float>& pending,
                                  const std::vector<float>& audio,
                                  const std::vector<size_t>& activeOutputIndices,
                                  double ringFillPercent,
                                  bool warmPendingRealAudio,
                                  std::vector<float>* pushedRealAudio)
{
    if (!engine) return 0;
    if (audio.empty() && !warmPendingRealAudio) return 0;
    const double outRate = std::max(8000.0, static_cast<double>(engine->getSampleRate()));
    const size_t phase2FrameSamples = std::max<size_t>(160,
        static_cast<size_t>(outRate * 0.020 + 0.5));
    // P25 Phase 2 decode windows are bursty and can arrive late.  Feed whole
    // 20 ms vocoder frames through the bounded jitter path so sustain mode
    // cannot dribble 5-10 ms fragments or stockpile stale speech.
    return pushP25LiveStreamingAudio(engine, pending, audio, activeOutputIndices,
                                     phase2FrameSamples, ringFillPercent,
                                      warmPendingRealAudio, pushedRealAudio);
}


std::vector<float> p25Phase2SpeakerAudioForQueue(
    P25Phase2SpeakerPendingQueue& queue,
    const P25VoiceAudioBlock& block,
    const std::vector<float>& audio,
    size_t frameSize)
{
    if (audio.empty() || frameSize == 0 || block.phase2EmittedSpeechOrdinals.empty()) {
        return audio;
    }

    const size_t pcmFrames = audio.size() / frameSize;
    const size_t ordinalFrames = block.phase2EmittedSpeechOrdinals.size();
    if (pcmFrames == 0 || ordinalFrames == 0) {
        return {};
    }
    const size_t mappedFrames = std::min(pcmFrames, ordinalFrames);
    // If a block contains unordinaled leading concealment/context PCM, map the
    // ordinals to the trailing real speech frames.  Failing open here replays
    // stale overlap windows into voicetest/GUI output.
    const size_t audioFrameOffset = (ordinalFrames < pcmFrames)
        ? (pcmFrames - ordinalFrames)
        : 0u;

    std::vector<float> filtered;
    filtered.reserve(mappedFrames * frameSize);
    for (size_t i = 0; i < mappedFrames; ++i) {
        const int64_t ordinal = block.phase2EmittedSpeechOrdinals[i];
        const size_t audioFrameIndex = audioFrameOffset + i;
        if (ordinal < 0) {
            filtered.insert(filtered.end(),
                            audio.begin() + static_cast<std::ptrdiff_t>(audioFrameIndex * frameSize),
                            audio.begin() + static_cast<std::ptrdiff_t>((audioFrameIndex + 1) * frameSize));
            continue;
        }
        if (!queue.nextSpeechOrdinalKnown) {
            queue.nextSpeechOrdinalKnown = true;
            queue.nextSpeechOrdinal = ordinal;
        }
        if (ordinal < queue.nextSpeechOrdinal) {
            continue;
        }
        if (ordinal > queue.nextSpeechOrdinal) {
            queue.nextSpeechOrdinal = ordinal;
        }
        filtered.insert(filtered.end(),
                        audio.begin() + static_cast<std::ptrdiff_t>(audioFrameIndex * frameSize),
                        audio.begin() + static_cast<std::ptrdiff_t>((audioFrameIndex + 1) * frameSize));
        queue.nextSpeechOrdinal = ordinal + 1;
    }
    return filtered;
}


void p25Phase2ResetPlayoutBridge(Receiver& rx) noexcept
{
    auto& tail = rx.p25SessionState.audioTail;
    tail.lastPlayoutBridgeMs = 0;
    tail.consecutivePlayoutBridgeFrames = 0;
    tail.lastSpeakerKey = {};
    tail.haveLastSpeakerKey = false;
    tail.playoutBridgeEligible = false;
}


void p25Phase2RememberLastEmittedSample(Receiver& rx,
                                               const P25P2CallAudioKey& key,
                                               const std::vector<float>& pcm,
                                               bool armPlayoutBridge) noexcept
{
    if (pcm.empty() || !std::isfinite(pcm.back()) || !key.valid()) return;
    auto& tail = rx.p25SessionState.audioTail;
    const bool sameExistingKey = tail.haveLastSpeakerKey && tail.lastSpeakerKey == key;
    tail.lastEmittedSample = pcm.back();
    tail.haveLastEmittedSample = true;
    tail.lastSpeakerKey = key;
    tail.haveLastSpeakerKey = true;
    tail.playoutBridgeEligible =
        armPlayoutBridge ? true : (sameExistingKey && tail.playoutBridgeEligible);
}


void p25Phase2NoteQueuedSpeakerPcmPushed(Receiver& rx,
                                                 const std::vector<float>& pcm,
                                                 qint64 nowMs) noexcept
{
    if (pcm.empty()) return;
    auto& tail = rx.p25SessionState.audioTail;
    tail.lastPlayoutBridgeMs = 0;
    tail.consecutivePlayoutBridgeFrames = 0;
    const P25P2CallAudioKey key =
        p25CurrentPhase2AudioKey(rx, p25Phase2VoiceSchedulerNominalHz(rx));
    p25Phase2RememberLastEmittedSample(rx, key, pcm, false);
    auto& sustain = rx.p25SessionState.sustain;
    if (sustain.sessionStartMs == 0) {
        sustain.sessionStartMs = nowMs;
    }
    sustain.hadSuccessfulEmit = true;
    sustain.lastEmitMs = nowMs;
    rx.p25Phase2CallHadSpeakerAudio = true;
    gP25AudioLastSpeakerOutputMs.store(nowMs, std::memory_order_relaxed);
}


bool p25Phase2PlayoutBridgeAllowed(const Receiver& rx, qint64 nowMs) noexcept
{
    if (!rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2 || rx.p25VoiceEncrypted) return false;

    const auto& sustain = rx.p25SessionState.sustain;
    if (!sustain.hadSuccessfulEmit || sustain.lastEmitMs <= 0) return false;
    const auto& tail = rx.p25SessionState.audioTail;
    const P25P2CallAudioKey currentKey =
        p25CurrentPhase2AudioKey(rx, p25Phase2VoiceSchedulerNominalHz(rx));
    if (!tail.playoutBridgeEligible ||
        !tail.haveLastSpeakerKey ||
        !currentKey.valid() ||
        !(tail.lastSpeakerKey == currentKey)) {
        return false;
    }

    const qint64 sinceLastEmitMs = nowMs - sustain.lastEmitMs;
    // Bridge is only an underrun guard.  It must never build hundreds of
    // milliseconds of queued silence ahead of the next selected-slot PCM.
    if (sinceLastEmitMs < 0 || sinceLastEmitMs > 4500) return false;

    const bool activeClearTail =
        p25Phase2SessionSpeakerSustainActive(rx) ||
        p25Phase2AudioTailGraceActive(rx) ||
        (rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear &&
         !rx.p25VoiceEncrypted);
    if (!activeClearTail) return false;

    // ~4.5 s of 20 ms silence frames max while waiting for the next island.
    if (tail.consecutivePlayoutBridgeFrames >= 225) return false;
    if (sinceLastEmitMs > 3500 &&
        (tail.consecutiveEmptyFeedWindows >= 48 ||
         tail.consecutiveNoForwardFedWindows >= 48)) {
        return false;
    }
    return true;
}


// Preserve pending PCM + AudioEngine ring across normal Phase-2 empty windows
// (opposite-slot dwell, waiting clear grant, short sync holes). Field
// 20260720_080118: live logged gate=emit pushes with activeOutputs=1 while the
// user heard total silence; offline voicetest recovered high-RMS clear WAV from
// the same IQ. Root cause: empty-audio results cleared the ring (and often
// pending) before the callback could play just-pushed PCM, racing the silence
// bridge. Do not invent-PLC — only stop destroying real selected-slot audio.
bool p25Phase2ShouldPreserveLivePlaybackBuffers(const Receiver& rx,
                                                       qint64 nowMs) noexcept
{
    if (!rx.p25VoicePhase2 || rx.p25VoiceEncrypted) return false;
    // Latched-clear calls must keep pending/ring across empty/gated windows.
    if (rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear) return true;
    if (rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted) return true;
    if (p25Phase2SessionSpeakerSustainActive(rx)) return true;
    if (p25Phase2EstablishedClearVoiceStreamingLocked(rx)) return true;
    if (p25Phase2PlayoutBridgeAllowed(rx, nowMs)) return true;
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) return true;
    const auto& sustain = rx.p25SessionState.sustain;
    if (sustain.hadSuccessfulEmit && sustain.lastEmitMs > 0 &&
        nowMs - sustain.lastEmitMs <= 1600) {
        return true;
    }
    return false;
}


std::vector<float> p25Phase2MakePlayoutBridgeAudio(Receiver& rx,
                                                          double outputRateHz,
                                                          size_t bridgeSamples)
{
    // Clock-only zeros.  Fading the last speech sample into the hole (011706)
    // sounded like a chirp/garble between real emits.  SDRTrunk does not
    // manufacture PCM for opposite-slot dwell; the ring just waits for the
    // next selected-slot frame.
    (void)rx;
    (void)outputRateHz;
    return std::vector<float>(bridgeSamples, 0.0f);
}


size_t pushP25Phase2PlayoutBridge(AudioEngine* engine,
                                         Receiver& rx,
                                         std::vector<float>& pending,
                                         const std::vector<size_t>& activeOutputIndices,
                                         double ringFillPercent = -1.0,
                                         size_t frameSize = 240)
{
    if (!engine || engine->activeOutputCount() == 0 || frameSize == 0) return 0;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!p25Phase2PlayoutBridgeAllowed(rx, nowMs)) return 0;

    const double outRate = std::max(8000.0, static_cast<double>(engine->getSampleRate()));
    const size_t effectiveFrameSize = std::max(frameSize,
        static_cast<size_t>(outRate * 0.020 + 0.5));
    // Match real PCM pushing: bridge decisions must be based on the live ring
    // sample count, not a diagnostic percentage snapshot.
    (void)ringFillPercent;
    size_t queuedNow = engine->getRingQueuedSamples();

    const size_t bridgeTargetSamples = std::max(effectiveFrameSize * 16,
        static_cast<size_t>(outRate * 0.320));
    if (queuedNow + pending.size() >= bridgeTargetSamples) return 0;

    const size_t deficit = bridgeTargetSamples - queuedNow - pending.size();
    const size_t maxBridgeSamples = std::max(effectiveFrameSize * 4,
        static_cast<size_t>(outRate * 0.080));
    size_t bridgeSamples = std::min(deficit, maxBridgeSamples);
    bridgeSamples = (bridgeSamples / effectiveFrameSize) * effectiveFrameSize;
    if (bridgeSamples < effectiveFrameSize) return 0;

    const auto bridge = p25Phase2MakePlayoutBridgeAudio(rx, outRate, bridgeSamples);
    size_t pushed = 0;
    while (pushed + effectiveFrameSize <= bridge.size()) {
        engine->pushBridgeAudioToActiveOutputs(bridge.data() + pushed, effectiveFrameSize, activeOutputIndices);
        pushed += effectiveFrameSize;
    }
    if (pushed > 0) {
        auto& tail = rx.p25SessionState.audioTail;
        const size_t samplesPerFrame = static_cast<size_t>(
            std::max(160.0, 160.0 * (outRate / 8000.0) + 0.5));
        tail.lastPlayoutBridgeMs = nowMs;
        tail.consecutivePlayoutBridgeFrames += static_cast<int>(
            std::max<size_t>(1, (pushed + samplesPerFrame - 1) / samplesPerFrame));
    }
    return pushed;
}


size_t p25TopUpSpeakerPlaybackRing(AudioEngine* engine,
                                          P25SpeakerPendingMap& pendingByRx,
                                          const std::function<bool(const ReceiverSessionKey&)>& sessionActive,
                                          size_t* realAudioPushed,
                                          size_t* bridgeAudioPushed)
{
    if (realAudioPushed) *realAudioPushed = 0;
    if (bridgeAudioPushed) *bridgeAudioPushed = 0;
    if (!engine || engine->activeOutputCount() == 0) return 0;

    const double outRate = std::max(8000.0, static_cast<double>(engine->getSampleRate()));
    const size_t phase2FrameSamples = std::max<size_t>(160,
        static_cast<size_t>(outRate * 0.020 + 0.5));
    size_t totalPushed = 0;
    for (auto it = pendingByRx.begin(); it != pendingByRx.end(); ++it) {
        if (!sessionActive(it->first)) continue;
        auto* rx = const_cast<Receiver*>(it->first.receiver);
        if (!rx) continue;
        if (it->second.samples.size() >= phase2FrameSamples) {
            std::vector<float> pushedRealAudio;
            const size_t pushed = pushP25LiveStreamingAudio(engine,
                                                            it->second.samples,
                                                            {},
                                                            rx->audioOutputIndices,
                                                            phase2FrameSamples,
                                                            -1.0,
                                                            true,
                                                            &pushedRealAudio);
            totalPushed += pushed;
            if (realAudioPushed) *realAudioPushed += pushed;
            if (!pushedRealAudio.empty()) {
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                p25Phase2NoteQueuedSpeakerPcmPushed(*rx, pushedRealAudio, nowMs);
            }
        }
        // Do not queue clock-only silence behind decoded speech. It extends
        // the speech timeline and delays the next real frame even when the
        // callback has not underrun. The callback already zero-fills a true
        // underrun; protocol erasures remain part of the decoder's PCM.
    }
    return totalPushed;
}



// Rolling IQ window: see P25RollingIq.h (ISS-0004 Phase 4)


std::vector<float> resampleDecodedP25PcmWithState(P25AudioResamplerState& st,
                                                         const std::vector<float>& pcm,
                                                         double inputRate,
                                                         double outputRate)
{
    if (pcm.empty() || !std::isfinite(inputRate) || !std::isfinite(outputRate) ||
        inputRate <= 0.0 || outputRate <= 0.0) {
        return {};
    }

    // SDRTrunk addAudio()s JMBE floats with no per-block AGC. mbelib PCM is
    // already mapped to [-1, 1] in normalizedMbelibPcm(). A second peak
    // normalize + tanh here pumped each Voice4 island to full scale and
    // added harmonic "filler" in quiet/concealment frames.
    (void)0;
    const double gain = 1.0;

    if (std::abs(st.lastInputRate - inputRate) > 1.0 ||
        std::abs(st.lastOutputRate - outputRate) > 1.0) {
        st.phase = 0.0;
        st.histYm2 = st.histYm1 = st.histY0 = 0.0f;
        st.haveHist = false;
        st.dcBlockX1 = 0.0f;
        st.dcBlockY1 = 0.0f;
        st.lastInputRate = inputRate;
        st.lastOutputRate = outputRate;
    }

    // High-quality cubic resampler (Catmull-Rom / Hermite) matching Demod.cpp streaming cubic.
    // Addresses audit point 4: linear/zero-order causes metallic aliasing on 8kHz->48kHz voice.
    // Stateful across 20ms IMBE/AMBE frames for click-free sustained P25 audio.
    // Includes per-block peak normalize + clamp for safe AudioEngine push.
    const double step = inputRate / outputRate;
    const double frameEnd = static_cast<double>(pcm.size());
    const double remainingInput = frameEnd - st.phase;
    if (remainingInput <= 0.0) {
        st.phase = 0.0;
        return {};
    }
    // Count output samples deterministically.  The previous ceil() on a raw
    // floating ratio occasionally turned exact 8 kHz -> 48 kHz AMBE blocks
    // into 960*n + 1 samples, leaving a sub-frame tail in the speaker queue.
    const size_t expected = std::max<size_t>(1, static_cast<size_t>(
        std::ceil((remainingInput / std::max(step, 1e-12)) - 1e-9)));
    std::vector<float> out;
    out.reserve(expected);
    constexpr double kP25AudioPi = 3.14159265358979323846;
    const float dcPole = static_cast<float>(std::exp(-2.0 * kP25AudioPi * 45.0 / outputRate));

    auto getY = [&](long i) -> float {
        if (i < 0) {
            if (!st.haveHist) return 0.0f;
            if (i == -1) return st.histY0;
            if (i == -2) return st.histYm1;
            if (i == -3) return st.histYm2;
            return 0.0f;
        }
        if ((size_t)i >= pcm.size()) return pcm.empty() ? 0.0f : pcm.back();
        return std::isfinite(pcm[static_cast<size_t>(i)]) ? pcm[static_cast<size_t>(i)] : 0.0f;
    };

    for (size_t n = 0; n < expected; ++n) {
        double pos = st.phase;
        long idx = static_cast<long>(std::floor(pos));
        double frac = pos - static_cast<double>(idx);
        float ym1 = getY(idx - 1);
        float y0  = getY(idx);
        float y1  = getY(idx + 1);
        float y2  = getY(idx + 2);
        float t = static_cast<float>(frac), t2 = t*t, t3 = t2*t;
        float c0 = y0;
        float c1 = 0.5f * (y1 - ym1);
        float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        float v = (c0 + c1 * t + c2 * t2 + c3 * t3) * static_cast<float>(gain);
        const float hp = v - st.dcBlockX1 + dcPole * st.dcBlockY1;
        st.dcBlockX1 = v;
        st.dcBlockY1 = std::isfinite(hp) ? hp : 0.0f;
        v = std::clamp(st.dcBlockY1, -1.0f, 1.0f);
        out.push_back(v);
        st.phase += step;
    }

    st.phase -= frameEnd;
    if (std::abs(st.phase) < 1e-8) {
        st.phase = 0.0;
    }
    // Do not aggressively reset phase to 0 during a call; that can introduce small
    // discontinuities in the resampled stream making "blocky" / not-joined audio.
    // Only reset on rate change (above). Allow fractional/negative for correct
    // history handoff to next 160-sample mbelib block.
    if (st.phase < -10.0 || st.phase > 200.0) {
        st.phase = 0.0;  // only on extreme drift
    }

    // Update cubic history from end of this block for next frame (stateful, no clicks)
    if (!pcm.empty()) {
        const size_t n = pcm.size();
        st.histYm2 = (n >= 3) ? pcm[n-3] : (st.haveHist ? st.histYm2 : 0.0f);
        st.histYm1 = (n >= 2) ? pcm[n-2] : (st.haveHist ? st.histYm1 : 0.0f);
        st.histY0  = pcm.back();
        st.haveHist = true;
    }

    return out;
}


std::vector<float> resampleDecodedP25Pcm(Receiver& rx,
                                                const std::vector<float>& pcm,
                                                double inputRate,
                                                double outputRate)
{
    return resampleDecodedP25PcmWithState(rx.p25SessionState.resampler, pcm, inputRate, outputRate);
}


bool p25AmbeDecodeFrameLooksUsable(const P25VoiceDecodeResult& decoded)
{
    if (decoded.status != P25VoiceDecodeStatus::Decoded || decoded.pcm.empty()) return false;

    double peak = 0.0;
    double sum2 = 0.0;
    for (float sample : decoded.pcm) {
        if (!std::isfinite(sample)) return false;
        const double v = static_cast<double>(sample);
        peak = std::max(peak, std::abs(v));
        sum2 += v * v;
    }
    const double rms = std::sqrt(sum2 / static_cast<double>(decoded.pcm.size()));
    // Fresh speech proof is stricter than speaker safety.  OP25 still advances
    // every AMBE time slot, but erasure/repeat-grade frames become concealment
    // rather than authoritative new speech.
    // Field captures showed that hard-dropping selected-slot AMBE frames creates
    // 20-80 ms islands and underruns. Feed the selected slot in order, but only
    // treat low-error non-repeat frames as fresh speech proof.
    if (decoded.totalErrors > 3) return false;
    if (decoded.message.find('R') != std::string::npos ||
        decoded.message.find('E') != std::string::npos) {
        return false;
    }
    if (peak > kP25DecodedAudioSafeMaxPeak || rms > kP25DecodedAudioSafeMaxRms) return false;
    return true;
}


bool p25DecodedAmbePcmLooksSafeForSpeaker(const P25VoiceDecodeResult& decoded) noexcept
{
    if (decoded.status != P25VoiceDecodeStatus::Decoded || decoded.pcm.empty()) return false;

    double peak = 0.0;
    double sum2 = 0.0;
    for (float sample : decoded.pcm) {
        if (!std::isfinite(sample)) return false;
        const double v = static_cast<double>(sample);
        peak = std::max(peak, std::abs(v));
        sum2 += v * v;
    }
    const double rms = std::sqrt(sum2 / static_cast<double>(decoded.pcm.size()));
    return peak <= kP25DecodedAudioSafeMaxPeak && rms <= kP25DecodedAudioSafeMaxRms;
}



P25Phase2AmbeInputQuality p25Phase2AmbeInputQualityFromLive(const P25LiveDecodeResult& live) noexcept
{
    P25Phase2AmbeInputQuality q;
    q.known = live.stats.softDecisionSymbols > 0 &&
        std::isfinite(live.stats.softDecisionQuality);
    q.softDecisionQuality = std::isfinite(live.stats.softDecisionQuality)
        ? live.stats.softDecisionQuality
        : 0.0;
    q.softDecisionSymbols = live.stats.softDecisionSymbols;
    q.softLowConfidenceSymbols = live.stats.softLowConfidenceSymbols;
    q.softLowConfidenceRatio = q.softDecisionSymbols > 0
        ? static_cast<double>(q.softLowConfidenceSymbols) /
            static_cast<double>(q.softDecisionSymbols)
        : 0.0;
    q.cqpskPhaseErrorRmsRad = std::isfinite(live.stats.cqpskPhaseErrorRmsRad)
        ? live.stats.cqpskPhaseErrorRmsRad
        : 0.0;
    q.bestPhase2SyncErrors = live.stats.bestPhase2SyncErrors;
    return q;
}


P25Phase2AmbeInputQuality p25Phase2AmbeInputQualityForCodeword(
    const P25LiveDecodeResult& live,
    const P25Phase2Burst& burst,
    const P25Phase2VoiceCodeword& codeword) noexcept
{
    P25Phase2AmbeInputQuality q = p25Phase2AmbeInputQualityFromLive(live);
    if (codeword.inputQualityKnown &&
        codeword.inputSoftDecisionSymbols > 0 &&
        std::isfinite(codeword.inputSoftDecisionQuality)) {
        q.known = true;
        q.softDecisionQuality = codeword.inputSoftDecisionQuality;
        q.softDecisionSymbols = codeword.inputSoftDecisionSymbols;
        q.softLowConfidenceSymbols = codeword.inputSoftLowConfidenceSymbols;
        q.softLowConfidenceRatio = q.softDecisionSymbols > 0
            ? static_cast<double>(q.softLowConfidenceSymbols) /
                static_cast<double>(q.softDecisionSymbols)
            : 0.0;
    }
    if (burst.syncErrors >= 0) {
        q.bestPhase2SyncErrors = burst.syncErrors;
    }
    return q;
}


P25Phase2AmbeInputQuality p25Phase2AmbeInputQualityFromPending(
    const P25P2PendingAmbeFrame& pending) noexcept
{
    P25Phase2AmbeInputQuality q;
    q.known = pending.inputQualityKnown &&
        pending.inputSoftDecisionSymbols > 0 &&
        std::isfinite(pending.inputSoftDecisionQuality);
    q.softDecisionQuality = std::isfinite(pending.inputSoftDecisionQuality)
        ? pending.inputSoftDecisionQuality
        : 0.0;
    q.softDecisionSymbols = pending.inputSoftDecisionSymbols;
    q.softLowConfidenceSymbols = pending.inputSoftLowConfidenceSymbols;
    q.softLowConfidenceRatio = q.softDecisionSymbols > 0
        ? static_cast<double>(q.softLowConfidenceSymbols) /
            static_cast<double>(q.softDecisionSymbols)
        : 0.0;
    q.cqpskPhaseErrorRmsRad = std::isfinite(pending.inputCqpskPhaseErrorRmsRad)
        ? pending.inputCqpskPhaseErrorRmsRad
        : 0.0;
    q.bestPhase2SyncErrors = pending.inputBestPhase2SyncErrors;
    return q;
}


P25Phase2AmbeInputQuality p25Phase2AmbeInputQualityFromSpeechInput(
    const P25Phase2SequencerSpeechInput& input) noexcept
{
    P25Phase2AmbeInputQuality q;
    q.known = input.inputQualityKnown &&
        input.inputSoftDecisionSymbols > 0 &&
        std::isfinite(input.inputSoftDecisionQuality);
    q.softDecisionQuality = std::isfinite(input.inputSoftDecisionQuality)
        ? input.inputSoftDecisionQuality
        : 0.0;
    q.softDecisionSymbols = input.inputSoftDecisionSymbols;
    q.softLowConfidenceSymbols = input.inputSoftLowConfidenceSymbols;
    q.softLowConfidenceRatio = q.softDecisionSymbols > 0
        ? static_cast<double>(q.softLowConfidenceSymbols) /
            static_cast<double>(q.softDecisionSymbols)
        : 0.0;
    q.cqpskPhaseErrorRmsRad = std::isfinite(input.inputCqpskPhaseErrorRmsRad)
        ? input.inputCqpskPhaseErrorRmsRad
        : 0.0;
    q.bestPhase2SyncErrors = input.inputBestPhase2SyncErrors;
    return q;
}


void p25Phase2ApplyAmbeInputQualityToPending(
    const P25Phase2AmbeInputQuality& q,
    P25P2PendingAmbeFrame& pending) noexcept
{
    pending.inputQualityKnown = q.known;
    pending.inputSoftDecisionQuality = q.softDecisionQuality;
    pending.inputSoftDecisionSymbols = q.softDecisionSymbols;
    pending.inputSoftLowConfidenceSymbols = q.softLowConfidenceSymbols;
    pending.inputCqpskPhaseErrorRmsRad = q.cqpskPhaseErrorRmsRad;
    pending.inputBestPhase2SyncErrors = q.bestPhase2SyncErrors;
}


void p25Phase2ApplyAmbeInputQualityToSpeechInput(
    const P25Phase2AmbeInputQuality& q,
    P25Phase2SequencerSpeechInput& input) noexcept
{
    input.inputQualityKnown = q.known;
    input.inputSoftDecisionQuality = q.softDecisionQuality;
    input.inputSoftDecisionSymbols = q.softDecisionSymbols;
    input.inputSoftLowConfidenceSymbols = q.softLowConfidenceSymbols;
    input.inputCqpskPhaseErrorRmsRad = q.cqpskPhaseErrorRmsRad;
    input.inputBestPhase2SyncErrors = q.bestPhase2SyncErrors;
}


void p25Phase2ApplyAmbeInputQualityToValidationFrame(
    const P25Phase2AmbeInputQuality& q,
    P25Phase2AmbeValidationFrame& frame) noexcept
{
    frame.inputQualityKnown = q.known;
    frame.inputSoftDecisionQuality = q.softDecisionQuality;
    frame.inputSoftDecisionSymbols = q.softDecisionSymbols;
    frame.inputSoftLowConfidenceSymbols = q.softLowConfidenceSymbols;
    frame.inputSoftLowConfidenceRatio = q.softLowConfidenceRatio;
    frame.inputCqpskPhaseErrorRmsRad = q.cqpskPhaseErrorRmsRad;
    frame.inputBestPhase2SyncErrors = q.bestPhase2SyncErrors;
}


std::string p25Phase2AmbeInputQualityBlockReason(
    const P25Phase2AmbeInputQuality& q)
{
    if (!q.known) return {};
    if (!std::isfinite(q.softDecisionQuality)) return "soft-quality-invalid";

    // mbelib can synthesize plausible PCM from a 72-bit VCW even when the CQPSK
    // eye is collapsing. Field replay 20260809 showed softQ 0.18-0.38 with
    // 32-43% low-confidence symbols producing full-scale noise while Golay
    // totals still looked low. Do not use a flat quality floor, though: known
    // clear TG30302 windows with sync error <=1 and only ~16% low-confidence
    // symbols were being muted before mbelib's per-frame ECC/error checks could
    // preserve speech continuity.
    if (q.softDecisionQuality < 0.42) return "soft-quality-low";
    if (q.softDecisionQuality < 0.50 &&
        (q.softLowConfidenceRatio > 0.18 ||
         q.cqpskPhaseErrorRmsRad > 0.36 ||
         q.bestPhase2SyncErrors > 1)) {
        return "soft-quality-low";
    }
    if (q.softDecisionQuality < 0.62 && q.softLowConfidenceRatio > 0.20) {
        return "soft-quality-low-confidence";
    }
    if (q.softDecisionQuality < 0.70 && q.softLowConfidenceRatio > 0.35) {
        return "soft-quality-excess-low-confidence";
    }
    const bool marginalSynchronizedEye =
        q.bestPhase2SyncErrors >= 0 &&
        q.bestPhase2SyncErrors <= 1 &&
        q.softLowConfidenceRatio <= 0.18 &&
        q.cqpskPhaseErrorRmsRad <= 0.34;
    if (q.cqpskPhaseErrorRmsRad > 0.30 &&
        q.softDecisionQuality < 0.65 &&
        q.softLowConfidenceRatio > 0.10 &&
        !marginalSynchronizedEye) {
        return "cqpsk-eye-low-confidence";
    }
    return {};
}


bool p25Phase2AmbeInputQualityHardBlock(
    const P25Phase2AmbeInputQuality& q,
    const std::string& blockReason) noexcept
{
    if (!q.known || !std::isfinite(q.softDecisionQuality)) return false;
    if (blockReason == "soft-quality-invalid") return true;

    const bool badBurstSync = q.bestPhase2SyncErrors > 1;
    const bool collapsedEye = q.cqpskPhaseErrorRmsRad > 0.40;
    const bool deeplyUnreliableSymbols =
        q.softDecisionQuality < 0.28 &&
        q.softLowConfidenceRatio > 0.40;
    const bool heavilyErasedSymbols =
        q.softDecisionQuality < 0.32 &&
        q.softLowConfidenceRatio > 0.50;
    const bool lowQualityWithBadLock =
        q.softDecisionQuality < 0.42 &&
        q.softLowConfidenceRatio > 0.40 &&
        (badBurstSync || collapsedEye);
    const bool excessLowConfidence =
        blockReason == "soft-quality-excess-low-confidence" &&
        q.softLowConfidenceRatio > 0.50;

    return deeplyUnreliableSymbols ||
        heavilyErasedSymbols ||
        lowQualityWithBadLock ||
        excessLowConfidence;
}


bool p25Phase2AmbeInputQualityBypassAllowed(
    const Receiver& rx,
    const P25VoiceAudioBlock& out,
    const P25Phase2AmbeInputQuality& q,
    const std::string& blockReason) noexcept
{
    if (blockReason.empty() || blockReason == "soft-quality-invalid") return false;
    if (rx.p25VoiceEncrypted || out.skippedEncrypted || out.phase2TargetEssEncrypted || out.phase2WrongSlot) {
        return false;
    }

    const bool targetClearEstablished =
        rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear ||
        out.phase2TargetSessionAudioRelease ||
        (out.phase2TargetEssKnown && !out.phase2TargetEssEncrypted) ||
        out.phase2SdrtrunkLateEntryVoiceRelease ||
        out.phase2ExplicitClearGrantVoiceRelease;
    if (!targetClearEstablished) return false;

    const bool selectedSlotStructured =
        out.phase2TargetVoiceCodewords > 0 &&
        out.phase2TargetMaskedBursts > 0 &&
        (out.phase2TargetMacCrcValid ||
         out.phase2TargetEssKnown ||
         out.phase2TargetSessionAudioRelease ||
         out.phase2SuperframeBursts >= 6 ||
         out.phase2ThisWindowTargetMacCrcValid ||
         out.phase2ThisWindowTargetEssClear);
    if (!selectedSlotStructured) return false;

    const bool catastrophicallyWeak =
        (q.softDecisionQuality < 0.20 && q.softLowConfidenceRatio > 0.60) ||
        p25Phase2AmbeInputQualityHardBlock(q, blockReason) ||
        (q.cqpskPhaseErrorRmsRad > 0.55 && q.softDecisionQuality < 0.35) ||
        (q.bestPhase2SyncErrors > 2 && q.softDecisionQuality < 0.35);
    return !catastrophicallyWeak;
}


// Input-quality concealment: when RF confidence is too poor to trust the AMBE
// bits, advance the 20 ms speaker timeline with silence.  Repeating the previous
// speech frame here sounds like doubled/blocky chatter and can be mistaken for
// valid voice; bad RF must not mutate mbelib or synthesize stale speech.
void p25Phase2AppendPlcBlock(Receiver& rx,
                                    double outputRateHz,
                                    P25VoiceAudioBlock& out)
{
    (void)rx;
    const size_t samplesPerFrame = static_cast<size_t>(
        std::max(160.0, 160.0 * (outputRateHz / 8000.0) + 0.5));
    out.audio.insert(out.audio.end(), samplesPerFrame, 0.0f);
    ++out.phase2EmittedPcmFrames;
    ++out.phase2ConcealmentFrames;
}


void p25Phase2AppendOppositeSlotSustainPlc(Receiver& rx,
                                                  P25VoiceAudioBlock& out,
                                                  double outputRateHz)
{
    // Intentionally disabled.  SDRTrunk binds one P25P2AudioModule per timeslot
    // and never invents companion-slot / empty-hop speech from last-good PCM.
    // Field 20260720: this path produced fed=0 emitPcm>0 synthetic audio that
    // sounded worse than raw decode gaps.
    (void)rx;
    (void)out;
    (void)outputRateHz;
}


bool p25Phase2AmbeProbeScoreLooksFinite(double score) noexcept
{
    return std::isfinite(score) && score > -100000.0;
}


size_t p25Phase2AmbeVariantVoiceSlotClamped(uint8_t voiceIndex) noexcept
{
    return std::min<size_t>(static_cast<size_t>(voiceIndex), 3u);
}


void p25Phase2RefreshAmbeVariantSummary(Receiver& rx, size_t slot) noexcept
{
    const size_t idx = std::min<size_t>(slot, rx.p25Phase2PreferredAmbeVariantByVoiceIndex.size() - 1u);
    rx.p25Phase2PreferredAmbeVariant = rx.p25Phase2PreferredAmbeVariantByVoiceIndex[idx];
    rx.p25Phase2PreferredAmbeVariantHits = rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex[idx];
    rx.p25Phase2PreferredAmbeVariantMisses = rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex[idx];
}


void p25Phase2ResetAmbeVariantLock(Receiver& rx, uint8_t voiceIndex, bool resetDecoder)
{
    const size_t slot = p25Phase2AmbeVariantVoiceSlotClamped(voiceIndex);
    rx.p25Phase2PreferredAmbeVariantByVoiceIndex[slot] = -1;
    rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex[slot] = 0;
    rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex[slot] = 0;
    p25Phase2RefreshAmbeVariantSummary(rx, slot);
    // Mid-call vocoder reconstruct is forbidden.  resetDecoder is ignored on
    // the live path; call-boundary resets go through resetP25TrafficSession.
    (void)resetDecoder;
}


void p25Phase2LockAmbeVariant(Receiver& rx, uint8_t voiceIndex, int variant, bool resetDecoder)
{
    if (variant < 0) return;
    const size_t slot = p25Phase2AmbeVariantVoiceSlotClamped(voiceIndex);
    const bool changed = rx.p25Phase2PreferredAmbeVariantByVoiceIndex[slot] != variant;
    // Live audio always uses canonical variant 0.  Non-zero requests are
    // diagnostic counters only and must not feed the speaker or reset mbelib.
    if (variant != 0) {
        ++rx.p25DiagVariantChanged;
        variant = 0;
    }
    rx.p25Phase2PreferredAmbeVariantByVoiceIndex[slot] = variant;
    rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex[slot] =
        std::max(1, rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex[slot]);
    rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex[slot] = 0;
    p25Phase2RefreshAmbeVariantSummary(rx, slot);
    (void)resetDecoder;
    (void)changed;
}


constexpr std::array<int, 4> kP25Phase2AmbeProbeVariants = {0, 1, 3, 4};

double p25Phase2AmbeVariantProbeScore(const P25VoiceDecodeResult& decoded)
{
    if (!p25AmbeDecodeFrameLooksUsable(decoded)) return -1e12;
    // Prefer low Golay error heavily over RMS.  Wrong bit-order variants can
    // still produce energetic garbage PCM; error count is the real discriminator.
    double score = 40.0 - static_cast<double>(decoded.totalErrors) * 18.0
                        - static_cast<double>(decoded.errors) * 8.0;
    double rms = 0.0;
    for (float sample : decoded.pcm) {
        if (!std::isfinite(sample)) continue;
        const double v = static_cast<double>(sample);
        rms += v * v;
    }
    if (!decoded.pcm.empty()) {
        rms = std::sqrt(rms / static_cast<double>(decoded.pcm.size()));
        // Small RMS tie-break only — never enough to beat a cleaner variant.
        score += std::min(rms * 40.0, 3.0);
    }
    return score;
}


P25Phase2AmbeVariantProbe p25ProbeSinglePhase2AmbeVariant(const P25Phase2VoiceCodeword& codeword, int variant)
{
    P25Phase2AmbeVariantProbe probe;
    probe.variant = variant;
    P25AmbeVoiceDecoder throwaway;
    const auto ambeFrame = p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant(codeword, variant);
    const auto decoded = throwaway.decodeAmbe3600x2450Frame(ambeFrame);
    probe.status = static_cast<int>(decoded.status);
    probe.errors = decoded.errors;
    probe.totalErrors = decoded.totalErrors;
    probe.pcmSamples = decoded.pcm.size();
    for (float sample : decoded.pcm) {
        if (!std::isfinite(sample)) continue;
        const double v = static_cast<double>(sample);
        probe.pcmPeak = std::max(probe.pcmPeak, std::abs(v));
        probe.pcmRms += v * v;
    }
    if (!decoded.pcm.empty()) {
        probe.pcmRms = std::sqrt(probe.pcmRms / static_cast<double>(decoded.pcm.size()));
    }
    probe.finite = std::any_of(decoded.pcm.begin(), decoded.pcm.end(),
                               [](float s) { return std::isfinite(s); });
    probe.usable = p25AmbeDecodeFrameLooksUsable(decoded);
    probe.score = p25Phase2AmbeVariantProbeScore(decoded);
    return probe;
}



P25Phase2AmbeResolveResult p25ResolvePhase2AmbeFrame(Receiver& rx,
                                                            const P25Phase2VoiceCodeword& codeword,
                                                            P25VoiceAudioBlock& out)
{
    P25Phase2AmbeResolveResult result;
    const size_t slot = p25Phase2AmbeVariantVoiceSlotClamped(codeword.voiceIndex);
    int lockedVariant = rx.p25Phase2PreferredAmbeVariantByVoiceIndex[slot];

    // Canonical mapping only on the live path (variant 0).  Alternatives are
    // diagnostic counters and never feed the speaker or reset the vocoder.
    if (lockedVariant < 0) {
        result.probed = p25Phase2DeepTraceEnabled() || p25Phase2ValidationLoggingEnabled();
        if (result.probed) {
            for (int variant : kP25Phase2AmbeProbeVariants) {
                result.probes.push_back(p25ProbeSinglePhase2AmbeVariant(codeword, variant));
            }
        }
        p25Phase2LockAmbeVariant(rx, codeword.voiceIndex, 0, false);
    } else if (lockedVariant != 0) {
        ++out.phase2AmbeVariantChanges;
        ++rx.p25DiagVariantChanged;
        p25Phase2LockAmbeVariant(rx, codeword.voiceIndex, 0, false);
    }

    result.variant = 0;
    result.frame = p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant(codeword, result.variant);
    return result;
}


QString p25Phase2ValidationPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData + "/logs");
    return appData + "/logs/p25_phase2_validation.jsonl";
}


bool p25Phase2ValidationLoggingEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("SDR_TOWN_P25_VALIDATION_LOG").trimmed().toLower();
        const bool on = value == "1" || value == "true" || value == "yes" || value == "on";
        if (on) {
            spdlog::warn("P25 validation logging enabled: raw diagnostic symbols and metadata may be sensitive.");
        }
        return on;
    }();
    return enabled;
}


bool p25Phase2ValidationRedactionEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("SDR_TOWN_P25_VALIDATION_REDACT").trimmed().toLower();
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }();
    return enabled;
}


bool p25Phase2DeepTraceEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("SDR_TOWN_P25_DEEP_TRACE").trimmed().toLower();
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }();
    return enabled;
}


bool p25Phase2OffsetProbeLogAllowed(bool acquisitionOffsetProbe)
{
    const bool deepTrace = p25Phase2DeepTraceEnabled();
    if (!acquisitionOffsetProbe && !deepTrace) return false;

    static std::mutex logMutex;
    static qint64 lastLogMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 minSpacingMs = deepTrace ? 500 : 2000;
    std::lock_guard<std::mutex> lk(logMutex);
    if (nowMs - lastLogMs < minSpacingMs) return false;
    lastLogMs = nowMs;
    return true;
}


void rotateP25Phase2ValidationLogIfNeeded(const QString& path)
{
    static qint64 lastRotateCheckMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - lastRotateCheckMs < 5000) return;
    lastRotateCheckMs = nowMs;

    constexpr qint64 kMaxValidationLogBytes = 64LL * 1024LL * 1024LL;
    QFileInfo info(path);
    if (!info.exists() || info.size() <= kMaxValidationLogBytes) return;

    const QString rotated = path + ".1";
    QFile::remove(rotated);
    if (!QFile::rename(path, rotated)) {
        spdlog::warn("Failed to rotate P25 Phase 2 validation log at {}", path.toStdString());
    }
}


std::string p25CompactDibits(const std::vector<int>& dibits)
{
    std::string out;
    out.reserve(dibits.size());
    for (int d : dibits) out.push_back(static_cast<char>('0' + (d & 0x03)));
    return out;
}


std::string p25CompactBits(const std::array<uint8_t, 96>& bits)
{
    std::string out;
    out.reserve(bits.size());
    for (uint8_t bit : bits) out.push_back(bit ? '1' : '0');
    return out;
}


bool p25DecodePhase2AmbeFrameToAudio(Receiver& rx,
                                            const std::array<uint8_t, 96>& ambeFrame,
                                            double outputRateHz,
                                            P25VoiceAudioBlock& out,
                                            P25Phase2AmbeValidationFrame& frame,
                                            const P25Phase2AmbeInputQuality& inputQuality)
{
    ++out.phase2AmbeDecodeAttempts;
    frame.probeScore = 0.0;
    frame.ambeBits = p25CompactBits(ambeFrame);
    p25Phase2ApplyAmbeInputQualityToValidationFrame(inputQuality, frame);
    const std::string inputQualityBlockReason =
        p25Phase2AmbeInputQualityBlockReason(inputQuality);
    const bool bypassInputQualityBlock =
        p25Phase2AmbeInputQualityBypassAllowed(rx, out, inputQuality, inputQualityBlockReason);
    const bool hardInputQualityBlock =
        p25Phase2AmbeInputQualityHardBlock(inputQuality, inputQualityBlockReason);
    auto concealSelectedSlotTimeline = [&](const std::string& reason, bool inputQualityReject) {
        const bool blockWasEmpty = out.audio.empty() && out.phase2EmittedPcmFrames == 0;
        frame.accepted = false;
        frame.timelineEmitted = true;
        if (frame.message.empty()) {
            frame.message = reason.empty() ? "concealed" : ("concealed-" + reason);
        }
        p25Phase2AppendPlcBlock(rx, outputRateHz, out);
        ++out.phase2RejectedVoiceCodewords;
        if (inputQualityReject) {
            ++out.phase2InputQualityRejectedVoiceCodewords;
        }
        out.phase2AmbeRejected = true;
        if (blockWasEmpty && out.phase2SpeakerGateReason.empty()) {
            out.phase2SpeakerGateReason = reason;
        }
    };
    // SDRTrunk/JMBE: once the call is proven clear, every selected-slot
    // 72-bit frame is passed to the synthesizer. There is no softQ gate in
    // P25P2AudioModule.processAudio() or AMBEAudioCodec.getAudioWithMetadata().
    const bool sdrtrunkClearFeed =
        rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear ||
        out.phase2SecurityTrustedClear;
    if (hardInputQualityBlock) {
        frame.inputQualityAccepted = false;
        frame.inputQualityBlockReason = inputQualityBlockReason.empty()
            ? "hard-soft-quality"
            : ("hard-" + inputQualityBlockReason);
        concealSelectedSlotTimeline(frame.inputQualityBlockReason, true);
        return false;
    }
    if (!inputQualityBlockReason.empty() && !bypassInputQualityBlock && !sdrtrunkClearFeed) {
        frame.inputQualityAccepted = false;
        frame.inputQualityBlockReason = inputQualityBlockReason;
        concealSelectedSlotTimeline(inputQualityBlockReason, true);
        return false;
    }
    frame.inputQualityAccepted = true;
    if (bypassInputQualityBlock) {
        frame.inputQualityBlockReason = "bypassed-" + inputQualityBlockReason;
    } else if (!inputQualityBlockReason.empty() && sdrtrunkClearFeed) {
        frame.inputQualityBlockReason = "clear-feed-" + inputQualityBlockReason;
    }
    // Match the OP25/sdrtrunk vocoder contract: one accepted AMBE codeword
    // advances one persistent call/slot vocoder state.  A throwaway preflight
    // or per-frame reset breaks predictor continuity and turns marginal-but-real
    // speech into isolated islands.
    auto decoded = rx.p25AmbeVoiceDecoder.decodeAmbe3600x2450Frame(ambeFrame);
    frame.status = static_cast<int>(decoded.status);
    frame.errors = decoded.errors;
    frame.totalErrors = decoded.totalErrors;
    frame.message = decoded.message;
    for (float sample : decoded.pcm) {
        if (!std::isfinite(sample)) continue;
        const double v = static_cast<double>(sample);
        frame.pcmPeak = std::max(frame.pcmPeak, std::abs(v));
        frame.pcmRms += v * v;
    }
    if (!decoded.pcm.empty()) {
        frame.pcmRms = std::sqrt(frame.pcmRms / static_cast<double>(decoded.pcm.size()));
    }

    // Hard Voice path: feed and emit the selected-slot vocoder exactly at AMBE
    // cadence once the call is clear. Fresh-speech proof remains strict for
    // acquisition/security decisions, but SDRTrunk/JMBE and OP25 do not replace
    // every mbelib repeat/erasure/near-silent frame with a synthetic zero block.
    // Doing that made a valid continuous stream sound like disconnected words.
    // Treat those frames as quality debt, not as speaker cadence gaps.
    const bool strictFresh = p25AmbeDecodeFrameLooksUsable(decoded);
    const bool speakerSafe = p25DecodedAmbePcmLooksSafeForSpeaker(decoded);
    const bool nearSilentCodecPcm = frame.pcmPeak < 5.0e-4 && frame.pcmRms < 1.0e-4;
    const bool erasureOrMute =
        decoded.message.find('E') != std::string::npos ||
        decoded.message.find('M') != std::string::npos;
    const bool softRepeat =
        decoded.message.find('R') != std::string::npos;
    const bool codecConcealment = !strictFresh || softRepeat || erasureOrMute || nearSilentCodecPcm;
    if (!speakerSafe) {
        concealSelectedSlotTimeline("unsafe-codec-pcm", false);
        return false;
    }
    const bool emitAsSpeaker = speakerSafe;
    frame.accepted = emitAsSpeaker;
    if (codecConcealment && emitAsSpeaker) {
        ++out.phase2ConcealmentFrames;
        frame.message = frame.message.empty()
            ? "decoded-codec-concealment"
            : (frame.message + "-concealment");
    }

    out.phase2AmbeRejected = false;
    ++out.phase2AmbeAcceptedFrames;
    if (frame.variant == 0) {
        ++out.phase2AmbeAcceptedCanonicalFrames;
    } else {
        ++out.phase2AmbeAcceptedFallbackFrames;
    }
    auto block = resampleDecodedP25Pcm(rx, decoded.pcm, decoded.sampleRate, outputRateHz);
    out.audio.insert(out.audio.end(), block.begin(), block.end());
    if (!block.empty() && strictFresh) {
        rx.p25Phase2LastGoodPcm = block;
    }
    ++out.decodedFrames;
    ++out.phase2EmittedPcmFrames;
    frame.timelineEmitted = true;
    return emitAsSpeaker;
}


bool p25ProbePhase2AmbeFrameForDiagnostics(P25AmbeVoiceDecoder& decoder,
                                                  const std::array<uint8_t, 96>& ambeFrame,
                                                  P25VoiceAudioBlock& out,
                                                  P25Phase2AmbeValidationFrame& frame)
{
    ++out.phase2DiagnosticAmbeProbeAttempts;
    frame.variant = 0;
    frame.probeScore = 0.0;
    frame.ambeBits = p25CompactBits(ambeFrame);

    const auto decoded = decoder.decodeAmbe3600x2450Frame(ambeFrame);
    frame.status = static_cast<int>(decoded.status);
    frame.errors = decoded.errors;
    frame.totalErrors = decoded.totalErrors;
    frame.message = decoded.message;
    frame.pcmPeak = 0.0;
    frame.pcmRms = 0.0;
    for (float sample : decoded.pcm) {
        if (!std::isfinite(sample)) continue;
        const double v = static_cast<double>(sample);
        frame.pcmPeak = std::max(frame.pcmPeak, std::abs(v));
        frame.pcmRms += v * v;
    }
    if (!decoded.pcm.empty()) {
        frame.pcmRms = std::sqrt(frame.pcmRms / static_cast<double>(decoded.pcm.size()));
    }
    frame.accepted = p25AmbeDecodeFrameLooksUsable(decoded);
    if (frame.accepted) {
        ++out.phase2DiagnosticAmbeProbeAccepted;
    }
    return frame.accepted;
}


json p25AudioVectorMetricsJson(const std::vector<float>& audio)
{
    json out;
    out["samples"] = audio.size();
    if (audio.empty()) {
        out["finite"] = true;
        out["finiteSamples"] = 0;
        out["min"] = 0.0;
        out["max"] = 0.0;
        out["mean"] = 0.0;
        out["rms"] = 0.0;
        out["peak"] = 0.0;
        return out;
    }

    bool finite = true;
    size_t finiteSamples = 0;
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    double sum2 = 0.0;
    double peak = 0.0;
    for (float sample : audio) {
        if (!std::isfinite(sample)) {
            finite = false;
            continue;
        }
        const double v = static_cast<double>(sample);
        minValue = std::min(minValue, v);
        maxValue = std::max(maxValue, v);
        sum += v;
        sum2 += v * v;
        peak = std::max(peak, std::abs(v));
        ++finiteSamples;
    }
    if (finiteSamples == 0) {
        minValue = 0.0;
        maxValue = 0.0;
    }
    out["finite"] = finite;
    out["finiteSamples"] = finiteSamples;
    out["min"] = minValue;
    out["max"] = maxValue;
    out["mean"] = finiteSamples > 0 ? sum / static_cast<double>(finiteSamples) : 0.0;
    out["rms"] = finiteSamples > 0 ? std::sqrt(sum2 / static_cast<double>(finiteSamples)) : 0.0;
    out["peak"] = peak;
    return out;
}



std::mutex gCaptureP25PendingMutex;

void clearP25SessionScopedState(Receiver& rx)
{
    rx.p25SessionState.clearAll();
}


bool tryResetP25TrafficSessionNonBlocking(Receiver& rx, const char* reason, bool fullClear)
{
    const bool preserveFollow = rx.p25VoiceDecodeEnabled;
    const bool voiceDecodeEnabled = rx.p25VoiceDecodeEnabled;
    const bool voiceClearKnown = rx.p25VoiceClearKnown;
    const bool voiceEncrypted = rx.p25VoiceEncrypted;
    const uint32_t talkgroupId = rx.p25VoiceTalkgroupId;
    const uint32_t sourceId = rx.p25VoiceSourceId;
    const int64_t grantEpochMs = rx.p25VoiceGrantEpochMs;
    const uint64_t currentCallSessionId = rx.p25CurrentCallSessionId;
    const uint64_t pttGeneration = rx.p25PttGeneration;
    const bool phase2 = rx.p25VoicePhase2;
    const bool slotKnown = rx.p25VoiceTdmaSlotKnown;
    const uint8_t slot = rx.p25VoiceTdmaSlot;
    const bool grantedSlotImmutable = rx.p25Phase2GrantedSlotImmutable;
    const bool maskKnown = rx.p25VoiceMaskParamsKnown;
    const uint16_t nac = rx.p25VoiceNac;
    const uint32_t wacn = rx.p25VoiceWacn;
    const uint16_t systemId = rx.p25VoiceSystemId;
    const int64_t settleUntilMs = rx.p25VoiceSettleUntilMs;
    const int discardWindows = rx.p25VoiceDiscardWindows;
    const bool allowLateEntryProbe = rx.p25Phase2AllowLateEntryAudioProbe;
    const bool targetOffsetKnown = rx.p25Phase2TrafficTargetOffsetKnown;
    const double targetOffsetHz = rx.p25Phase2TrafficTargetOffsetHz;
    const int targetOffsetTrust = rx.p25Phase2TrafficTargetOffsetTrust;
    const int targetOffsetMisses = rx.p25Phase2TrafficTargetOffsetMisses;
    if (rx.tryResetP25TrafficSession(reason, fullClear)) {
        clearP25SessionScopedState(rx);
        if (preserveFollow) {
            rx.p25VoiceDecodeEnabled = voiceDecodeEnabled;
            rx.p25VoiceClearKnown = voiceClearKnown;
            rx.p25VoiceEncrypted = voiceEncrypted;
            rx.p25VoiceTalkgroupId = talkgroupId;
            rx.p25VoiceSourceId = sourceId;
            rx.p25VoiceGrantEpochMs = grantEpochMs;
            rx.p25CurrentCallSessionId = currentCallSessionId;
            rx.p25PttGeneration = pttGeneration;
            rx.p25VoicePhase2 = phase2;
            rx.p25VoiceTdmaSlotKnown = slotKnown;
            rx.p25VoiceTdmaSlot = slot;
            rx.p25Phase2GrantedSlotImmutable = grantedSlotImmutable;
            rx.p25VoiceMaskParamsKnown = maskKnown;
            rx.p25VoiceNac = nac;
            rx.p25VoiceWacn = wacn;
            rx.p25VoiceSystemId = systemId;
            rx.p25VoiceSettleUntilMs = settleUntilMs;
            rx.p25VoiceDiscardWindows = discardWindows;
            rx.p25Phase2AllowLateEntryAudioProbe = allowLateEntryProbe;
            rx.p25Phase2TrafficTargetOffsetKnown = targetOffsetKnown;
            rx.p25Phase2TrafficTargetOffsetHz = targetOffsetHz;
            rx.p25Phase2TrafficTargetOffsetTrust = targetOffsetTrust;
            rx.p25Phase2TrafficTargetOffsetMisses = targetOffsetMisses;
            rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));
            if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
                rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
            } else {
                rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
            }
        }
        rx.p25VoiceResetPending = false;
        return true;
    }
    rx.p25VoiceResetPending = true;
    return false;
}


P25Phase2AmbeEmitDedupeState& p25Phase2SyncAmbeEmitDedupeCallContext(Receiver& rx)
{
    auto& state = rx.p25SessionState.ambeDedupe;

    const uint32_t currentTg = rx.p25VoiceTalkgroupId;
    const uint32_t currentSource = rx.p25VoiceSourceId;
    const uint64_t currentCallSession = rx.p25CurrentCallSessionId;
    const int64_t currentGrantEpoch = rx.p25VoiceGrantEpochMs;
    const bool currentSlotKnown = rx.p25VoiceTdmaSlotKnown;
    const uint8_t currentSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
    const double currentVoiceFreqHz = p25Phase2VoiceSchedulerNominalHz(rx);
    const bool callChanged = state.talkgroupId != currentTg ||
                             state.callSessionId != currentCallSession ||
                             state.slotKnown != currentSlotKnown ||
                             (currentSlotKnown && state.slotKnown && state.slot != currentSlot) ||
                             (state.voiceFreqHz > 0.0 && currentVoiceFreqHz > 0.0 &&
                              std::abs(state.voiceFreqHz - currentVoiceFreqHz) > 25.0);

    // Cursor resets are already handled by the IQ stream/session reset paths.
    // Do not reset here just because an overlapped GUI decode window begins
    // behind the last emitted AMBE frame.
    if (callChanged) {
        // New traffic call, slot handoff, or frequency handoff.
        // sdrtrunk's P25P2 traffic channel/audio modules reset state at call/squelch
        // boundaries; carrying an overlap-de-dupe cursor across a new call suppresses
        // valid early voice frames and sounds like no/very little audio.  The AMBE
        // vocoder, resampler history, and PLC tail are also predictor state and
        // must not bleed across separate Phase-2 calls.
        // Slot changes mid-call are blocked when the grant slot is immutable;
        // a slot delta here means a real call-boundary reassignment.
        const bool slotFlip =
            state.slotKnown && currentSlotKnown && state.slot != currentSlot;
        const bool sameCarrier =
            state.voiceFreqHz > 0.0 && currentVoiceFreqHz > 0.0 &&
            std::abs(state.voiceFreqHz - currentVoiceFreqHz) <= 50.0;
        const bool companionMatchesNewSlot =
            rx.p25SessionState.pendingAudioOpposite.armed &&
            rx.p25SessionState.pendingAudioOpposite.key.slot == currentSlot;
        if (slotFlip) {
            ++rx.p25DiagSlotChanged;
        }
        if (slotFlip && sameCarrier && companionMatchesNewSlot) {
            // Same-RF companion promote: swap warmed modules, do not dual-reset.
            p25NotePhase2VocoderReset(rx, "companion-promote");
            p25Phase2PromoteCompanionModules(rx, "call-boundary-same-rf");
            state = P25Phase2AmbeEmitDedupeState{};
            rx.p25SessionState.audioTail = {};
            // Selected resampler is now former companion (already swapped).
            rx.p25SessionState.sustain = {};
            rx.p25SessionState.callSecurityLatch = P25CallSecurityLatch::Unknown;
            p25ClearPhase2RecentSecurityEvidence(rx);
            // Drop demoted (old selected) pending only.
            rx.p25SessionState.pendingAudioOpposite = {};
            p25Phase2ResetFrameSequencer(rx);
            rx.p25Phase2PreferredAmbeVariant = -1;
            rx.p25Phase2PreferredAmbeVariantHits = 0;
            rx.p25Phase2PreferredAmbeVariantMisses = 0;
            rx.p25Phase2PreferredAmbeVariantByVoiceIndex.fill(-1);
            rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex.fill(0);
            rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex.fill(0);
        } else {
            p25NotePhase2VocoderReset(rx, "call-boundary");
            state = P25Phase2AmbeEmitDedupeState{};
            rx.p25SessionState.audioTail = {};
            rx.p25SessionState.resampler = {};
            // Capture 20260808_021134: Clear latch + hadSuccessfulEmit survived
            // TG 30304→30302→12542 handoffs (dedupe reset only). Next call then
            // trusted-clear-released dual-slot ess=unknown VCWs → mostly garble
            // with rare clear words when the epoch happened to match.
            rx.p25SessionState.sustain = {};
            rx.p25SessionState.callSecurityLatch = P25CallSecurityLatch::Unknown;
            p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::CallIdentityChanged);
            p25ClearPhase2RecentSecurityEvidence(rx);
            rx.p25AmbeVoiceDecoder = P25AmbeVoiceDecoder();
            rx.p25AmbeVoiceDecoderOpposite = P25AmbeVoiceDecoder();
            rx.p25Phase2LastGoodPcm.clear();
            p25Phase2ResetFrameSequencer(rx);
            rx.p25Phase2PreferredAmbeVariant = -1;
            rx.p25Phase2PreferredAmbeVariantHits = 0;
            rx.p25Phase2PreferredAmbeVariantMisses = 0;
            rx.p25Phase2PreferredAmbeVariantByVoiceIndex.fill(-1);
            rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex.fill(0);
            rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex.fill(0);
        }
    }
    state.talkgroupId = currentTg;
    state.sourceId = currentSource;
    state.callSessionId = currentCallSession;
    state.grantEpochMs = currentGrantEpoch;
    state.slotKnown = currentSlotKnown;
    state.slot = currentSlot;
    state.voiceFreqHz = currentVoiceFreqHz;
    return state;
}


bool p25Phase2ShouldEmitAmbeFrame(Receiver& rx,
                                         uint64_t codewordAbsDibit,
                                         uint64_t codewordEndAbsDibit,
                                         bool haveAbsoluteDibits,
                                         bool rememberFrame = true)
{
    // De-dupe overlapped decode windows by recovered stream position only.  Do
    // not de-dupe by AMBE payload hash: valid speech commonly has repeated or
    // near-repeated AMBE payloads.  Adjacent Voice4 AMBE starts are 36-37
    // dibits apart (48 across one intra-burst hole).  40 dibits swallowed the
    // next real frame; 24 dibits (011706) starved unique selected-slot frames
    // into 300 ms islands.  12 matches same-start overlap without eating +36.
    constexpr uint64_t kPhase2DuplicateStartToleranceDibits = 12u;
    constexpr size_t kPhase2RecentAbsLimit = 128;

    auto& state = p25Phase2SyncAmbeEmitDedupeCallContext(rx);
    if (!haveAbsoluteDibits) {
        return true;
    }

    bool closeToPrior = false;
    for (uint64_t priorAbs : state.recentAbsDibits) {
        const uint64_t delta = codewordAbsDibit > priorAbs
            ? codewordAbsDibit - priorAbs
            : priorAbs - codewordAbsDibit;
        if (delta <= kPhase2DuplicateStartToleranceDibits) {
            closeToPrior = true;
            break;
        }
    }

    // Exclusive previous end. Equality is the next contiguous AMBE start.
    // Allow 12-dibit backward eye wobble so a valid next frame whose recovered
    // start landed just inside the previous end is not starved. Same-frame
    // overlap still fails closeToPrior (12-dibit start match) or this test.
    constexpr uint64_t kPhase2ForwardWobbleDibits = 12u;
    const bool isForwardNew = (state.lastAbsDibit == 0 ||
        codewordAbsDibit + kPhase2ForwardWobbleDibits >= state.lastAbsDibit);
    if (!isForwardNew) {
        return false;
    }
    if (closeToPrior) {
        return false;
    }

    if (rememberFrame) {
        state.lastAbsDibit = std::max(state.lastAbsDibit, codewordEndAbsDibit);
        state.recentAbsDibits.push_back(codewordAbsDibit);
        if (state.recentAbsDibits.size() > kPhase2RecentAbsLimit) {
            state.recentAbsDibits.erase(state.recentAbsDibits.begin(),
                state.recentAbsDibits.begin() + static_cast<std::ptrdiff_t>(state.recentAbsDibits.size() - kPhase2RecentAbsLimit));
        }
    }
    return true;
}


void p25Phase2RememberEmittedAmbeFrame(Receiver& rx,
                                              uint64_t codewordAbsDibit,
                                              uint64_t codewordEndAbsDibit,
                                              bool haveAbsoluteDibits,
                                              const Phase2VoiceFrameKey* latticeKey = nullptr,
                                              int64_t nowMs = 0)
{
    (void)p25Phase2ShouldEmitAmbeFrame(rx, codewordAbsDibit, codewordEndAbsDibit,
                                       haveAbsoluteDibits, true);
    if (latticeKey == nullptr || nowMs <= 0) return;
    if (latticeKey->slot > 1u || latticeKey->burstIndex >= 12u || latticeKey->voiceIndex >= 4u) {
        return;
    }
    auto& state = p25Phase2SyncAmbeEmitDedupeCallContext(rx);
    P25Phase2AmbeEmitDedupeState::LatticeEmit rec;
    rec.slot = latticeKey->slot;
    rec.burstIndex = latticeKey->burstIndex;
    rec.voiceIndex = latticeKey->voiceIndex;
    rec.absKnown = haveAbsoluteDibits || latticeKey->streamDibitKnown;
    rec.absDibit = haveAbsoluteDibits ? codewordAbsDibit
        : (latticeKey->streamDibitKnown ? latticeKey->streamDibit : 0);
    rec.emitMs = nowMs;
    state.recentLatticeKeys.push_back(rec);
    constexpr int64_t kWallTtlMs = 2000;
    constexpr size_t kMaxKeys = 48;
    while (!state.recentLatticeKeys.empty() &&
           (nowMs - state.recentLatticeKeys.front().emitMs) > kWallTtlMs) {
        state.recentLatticeKeys.erase(state.recentLatticeKeys.begin());
    }
    if (state.recentLatticeKeys.size() > kMaxKeys) {
        state.recentLatticeKeys.erase(
            state.recentLatticeKeys.begin(),
            state.recentLatticeKeys.begin() + static_cast<std::ptrdiff_t>(
                state.recentLatticeKeys.size() - kMaxKeys));
    }
}


bool p25Phase2LatticeKeyAlreadyEmitted(Receiver& rx,
                                             const Phase2VoiceFrameKey& key,
                                             uint64_t codewordAbsDibit,
                                             bool haveAbsoluteDibits,
                                             int64_t nowMs)
{
    // Capture 20260907_073304 TG 10330 seq=389/401: independent CQPSK eyes
    // replayed overlap because recovered abs moved more than 12 dibits
    // (DEC-0008). Slot + superframe burst index + voiceIndex is the ISCH
    // lattice identity for one 360 ms fragment (SDRTrunk SuperFrameFragment).
    // Same identity within 1800 dibits (~300 ms, overlap 280 ms, one
    // superframe 2160 dibits) is a replay. Next superframe keeps the index
    // but is >= 2160 dibits later. DEC-0013.
    if (key.slot > 1u || key.burstIndex >= 12u || key.voiceIndex >= 4u) {
        return false;
    }
    constexpr uint64_t kSameSuperframeAbsDibits = 1800u;
    constexpr int64_t kWallTtlMs = 300;
    const bool absKnown = haveAbsoluteDibits || key.streamDibitKnown;
    const uint64_t absDibit = haveAbsoluteDibits ? codewordAbsDibit
        : (key.streamDibitKnown ? key.streamDibit : 0);
    const auto& state = p25Phase2SyncAmbeEmitDedupeCallContext(rx);
    for (const auto& prior : state.recentLatticeKeys) {
        if (prior.slot != key.slot ||
            prior.burstIndex != key.burstIndex ||
            prior.voiceIndex != key.voiceIndex) {
            continue;
        }
        if (absKnown && prior.absKnown) {
            const uint64_t delta = absDibit > prior.absDibit
                ? absDibit - prior.absDibit
                : prior.absDibit - absDibit;
            if (delta < kSameSuperframeAbsDibits) {
                return true;
            }
            continue;
        }
        if (nowMs > 0 && prior.emitMs > 0 &&
            (nowMs - prior.emitMs) >= 0 &&
            (nowMs - prior.emitMs) <= kWallTtlMs) {
            return true;
        }
    }
    return false;
}


bool p25Phase2AmbeStartDeltaLooksContinuous(uint64_t delta) noexcept
{
    // AMBE starts inside a Phase-2 Voice4 burst are 37/48/37 dibits apart.
    // Between selected-slot bursts the TDMA lattice can advance roughly 180 or
    // 360 dibits depending on where the slot pair sits in the 12-burst fragment.
    return (delta >= 24u && delta <= 72u) ||
           (delta >= 132u && delta <= 260u) ||
           (delta >= 300u && delta <= 430u);
}


size_t p25Phase2FeedGapPlcFrameCount(uint64_t deltaDibits) noexcept
{
    // Normal selected-slot AMBE spacing is ~36-48 dibits (or ~180/360 across
    // the companion slot). Larger holes are either a dropped target frame or
    // true silence — fill at most two 20 ms PLC blocks so we do not invent
    // speech across opposite-slot dwell or real PTT gaps.
    if (p25Phase2AmbeStartDeltaLooksContinuous(deltaDibits)) return 0;
    if (deltaDibits >= 70u && deltaDibits <= 140u) return 2;
    if (deltaDibits > 48u && deltaDibits < 70u) return 1;
    if (deltaDibits > 140u && deltaDibits <= 260u) return 1;
    return 0;
}


void p25Phase2FillFeedGapWithPlc(Receiver& rx,
                                        P25VoiceAudioBlock& out,
                                        double outputRateHz,
                                        uint64_t codewordAbsDibit,
                                        bool haveAbsoluteDibits)
{
    // Disabled: SDRTrunk does not invent AMBE/PCM for dibit gaps.  Cadence is
    // restored by feeding the next real selected-slot codeword into mbelib.
    (void)rx;
    (void)out;
    (void)outputRateHz;
    (void)codewordAbsDibit;
    (void)haveAbsoluteDibits;
}


void p25RecordPhase2AmbeFeedCadence(P25VoiceAudioBlock& out,
                                           uint64_t codewordAbsDibit,
                                           bool haveAbsoluteDibits)
{
    ++out.phase2FedToMbelib;
    if (out.phase2FedToMbelib == 1) {
        out.phase2FirstFedAbsDibit = codewordAbsDibit;
    }
    if (!haveAbsoluteDibits) return;

    if (out.phase2LastFedAbsDibit != 0) {
        if (codewordAbsDibit + 12u < out.phase2LastFedAbsDibit) {
            ++out.phase2FeedOrderIssues;
            ++out.phase2FeedGaps;
        } else {
            const uint64_t delta = codewordAbsDibit >= out.phase2LastFedAbsDibit
                ? codewordAbsDibit - out.phase2LastFedAbsDibit
                : out.phase2LastFedAbsDibit - codewordAbsDibit;
            if (!p25Phase2AmbeStartDeltaLooksContinuous(delta)) {
                ++out.phase2FeedGaps;
            }
        }
    }
    out.phase2LastFedAbsDibit = std::max(out.phase2LastFedAbsDibit, codewordAbsDibit);
}


json p25Phase2EssJson(const P25Phase2EssState& ess, bool redactSensitive = false)
{
    json out;
    out["known"] = ess.known;
    out["encrypted"] = ess.encrypted;
    out["algId"] = ess.algId;
    out["keyId"] = redactSensitive ? json(nullptr) : json(ess.keyId);
    out["fecValidated"] = ess.fecValidated;
    out["correctedSymbols"] = ess.correctedSymbols;
    std::vector<uint8_t> mi(ess.messageIndicator.begin(), ess.messageIndicator.end());
    out["messageIndicatorHex"] = redactSensitive ? std::string("<redacted>") : p25BytesToHex(mi).toStdString();
    out["redacted"] = redactSensitive;
    return out;
}


void writeP25Phase2ValidationRecord(const Receiver& rx,
                                           const P25LiveDecodeResult& live,
                                           const P25VoiceAudioBlock& audio,
                                           const std::vector<P25Phase2AmbeValidationFrame>& ambeFrames,
                                           double sampleRateHz,
                                           double centerFreqHz,
                                           double targetFreqHz,
                                           double outputRateHz)
{
    if (!rx.p25VoicePhase2 || live.stats.phase2Bursts == 0) return;
    const bool explicitValidationLog = p25Phase2ValidationLoggingEnabled();
    const bool autoAmbeRejectLog =
        rx.p25VoiceDecodeEnabled &&
        rx.p25VoicePhase2 &&
        audio.backendAvailable &&
        audio.phase2AmbeDecodeAttempts > 0 &&
        audio.phase2AmbeAcceptedFrames == 0 &&
        (audio.phase2TargetVoiceCodewords > 0 ||
         audio.phase2MaskedBursts > 0 ||
         live.stats.phase2MaskedBursts > 0 ||
         live.stats.phase2MacCrcValid > 0);
    const bool autoAmbePartialLog =
        rx.p25VoiceDecodeEnabled &&
        rx.p25VoicePhase2 &&
        audio.backendAvailable &&
        audio.phase2AmbeDecodeAttempts >= 4 &&
        audio.phase2AmbeAcceptedFrames > 0 &&
        audio.phase2AmbeAcceptedFrames < audio.phase2AmbeDecodeAttempts &&
        (audio.audio.size() > 0 ||
         audio.phase2TargetVoiceCodewords > 0 ||
         audio.phase2MaskedBursts > 0 ||
         live.stats.phase2MacCrcValid > 0);
    const bool autoDeepTraceLog =
        p25Phase2DeepTraceEnabled() &&
        rx.p25VoiceDecodeEnabled &&
        rx.p25VoicePhase2 &&
        (live.stats.phase2Bursts > 0 ||
         live.stats.phase2VoiceCodewords > 0 ||
         live.stats.phase2MacPdus > 0 ||
         audio.phase2Bursts > 0 ||
         audio.phase2VoiceCodewords > 0);
    if (!explicitValidationLog && !autoAmbeRejectLog && !autoAmbePartialLog && !autoDeepTraceLog) return;
    const bool finalSecurityGateRecord = !audio.phase2SecurityGateAction.empty();
    const bool detailedValidationRecord =
        explicitValidationLog || autoDeepTraceLog || autoAmbeRejectLog || autoAmbePartialLog;
    const bool redactRaw = p25Phase2ValidationRedactionEnabled() || !explicitValidationLog;

    static std::mutex validationThrottleMutex;
    static qint64 lastAutoWriteMs = 0;
    static qint64 lastExplicitWriteMs = 0;
    static qint64 lastFinalSecurityGateWriteMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    {
        std::lock_guard<std::mutex> lk(validationThrottleMutex);
        qint64& lastWriteMs = finalSecurityGateRecord
            ? lastFinalSecurityGateWriteMs
            : (explicitValidationLog ? lastExplicitWriteMs : lastAutoWriteMs);
        const qint64 minSpacingMs = finalSecurityGateRecord
            ? (explicitValidationLog ? 100 : 250)
            : (explicitValidationLog
                ? 250
                : (autoDeepTraceLog ? 1000 : (audio.decodedFrames > 0 ? 2000 : 3000)));
        // DEC-0071: opt-in offline forensics must retain every replay hop.
        const bool traceEveryWindow = explicitValidationLog &&
            qEnvironmentVariableIntValue("SDR_TOWN_P25_VALIDATION_ALL") == 1;
        if (!traceEveryWindow && nowMs - lastWriteMs < minSpacingMs) return;
        lastWriteMs = nowMs;
    }

    json record;
    record["schema"] = "sdr-town-p25-phase2-validation-v1";
    record["timeUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    record["autoGenerated"] = !explicitValidationLog;
    record["postSecurityGateRecord"] = finalSecurityGateRecord;
    record["reason"] = autoAmbeRejectLog ? "phase2-ambe-attempts-rejected"
        : (autoAmbePartialLog ? "phase2-ambe-partial-accept"
           : (autoDeepTraceLog ? "phase2-deep-trace" : "explicit-validation"));
    record["deepTrace"] = autoDeepTraceLog;
    record["talkgroupId"] = rx.p25VoiceTalkgroupId;
    record["centerFreqHz"] = centerFreqHz;
    record["targetFreqHz"] = targetFreqHz;
    record["sampleRateHz"] = sampleRateHz;
    record["outputRateHz"] = outputRateHz;
    record["demodPath"] = live.stats.demodPath;
    record["inputTargetOffsetHz"] = live.stats.inputTargetOffsetHz;
    record["effectiveTargetOffsetHz"] = targetFreqHz - centerFreqHz;
    record["trafficTargetOffsetLock"] = {
        {"known", rx.p25Phase2TrafficTargetOffsetKnown},
        {"offsetHz", rx.p25Phase2TrafficTargetOffsetHz},
        {"trust", rx.p25Phase2TrafficTargetOffsetTrust},
        {"misses", rx.p25Phase2TrafficTargetOffsetMisses},
    };
    record["channelSampleRateHz"] = live.stats.channelSampleRate;
    record["discriminatorMeanHz"] = live.stats.discriminatorMeanHz;
    record["diag"] = p25VoiceDiagLabel(audio.diag);
    record["preferredAmbeVariant"] = rx.p25Phase2PreferredAmbeVariant;
    record["preferredAmbeVariantHits"] = rx.p25Phase2PreferredAmbeVariantHits;
    record["preferredAmbeVariantMisses"] = rx.p25Phase2PreferredAmbeVariantMisses;
    record["preferredAmbeVariantsByVoiceIndex"] = json::array();
    record["preferredAmbeVariantHitsByVoiceIndex"] = json::array();
    record["preferredAmbeVariantMissesByVoiceIndex"] = json::array();
    for (size_t i = 0; i < rx.p25Phase2PreferredAmbeVariantByVoiceIndex.size(); ++i) {
        record["preferredAmbeVariantsByVoiceIndex"].push_back(rx.p25Phase2PreferredAmbeVariantByVoiceIndex[i]);
        record["preferredAmbeVariantHitsByVoiceIndex"].push_back(rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex[i]);
        record["preferredAmbeVariantMissesByVoiceIndex"].push_back(rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex[i]);
    }
    record["audioMetrics"] = p25AudioVectorMetricsJson(audio.audio);
    record["securityGate"] = {
        {"action", audio.phase2SecurityGateAction},
        {"preSecurityAudioSamples", audio.phase2PreSecurityAudioSamples},
        {"preSecurityDecodedFrames", audio.phase2PreSecurityDecodedFrames},
        {"postSecurityAudioSamples", audio.audio.size()},
        {"postSecurityDecodedFrames", audio.decodedFrames},
        {"pendingSamplesBefore", audio.phase2PendingAudioSamplesBefore},
        {"pendingSamplesAfter", audio.phase2PendingAudioSamplesAfter},
        {"pendingAmbeFramesReleased", audio.phase2PendingAmbeFramesReleased},
        {"trustedClear", audio.phase2SecurityTrustedClear},
        {"trustedEncrypted", audio.phase2SecurityTrustedEncrypted},
        {"unknownSecurity", audio.phase2SecurityUnknown},
        {"currentProbePcmUsable", audio.phase2CurrentProbePcmUsable},
        {"fieldAudioProbeAllowed", audio.phase2FieldAudioProbeAllowed},
        {"unknownProbeQualityOk", audio.phase2UnknownProbeQualityOk},
        {"unknownProbeBlockReason", audio.phase2UnknownProbeBlockReason},
        {"targetEssKnown", audio.phase2TargetEssKnown},
        {"targetEssEncrypted", audio.phase2TargetEssEncrypted},
        {"targetMacCrcValid", audio.phase2TargetMacCrcValid},
        {"targetSessionAudioRelease", audio.phase2TargetSessionAudioRelease},
        {"targetSecurityStateFromPtt", audio.phase2TargetSecurityStateFromPtt},
        {"currentFeedTrustedTargetBurst", audio.phase2CurrentFeedTrustedTargetBurst},
        {"sameCallSelectedTimeslotContinuation", audio.phase2SameCallSelectedTimeslotContinuation},
        {"sdrtrunkLateEntryVoiceRelease", audio.phase2SdrtrunkLateEntryVoiceRelease},
        {"explicitClearGrantVoiceRelease", audio.phase2ExplicitClearGrantVoiceRelease},
        {"speakerGateReason", audio.phase2SpeakerGateReason},
    };

    record["mask"] = {
        {"known", rx.p25VoiceMaskParamsKnown},
        {"decoderKnown", live.stats.phase2MaskParametersKnown},
        {"nac", rx.p25VoiceNac},
        {"wacn", rx.p25VoiceWacn},
        {"systemId", rx.p25VoiceSystemId},
        {"followedGrantSlotKnown", rx.p25VoiceTdmaSlotKnown},
        {"followedGrantSlot", rx.p25VoiceTdmaSlotKnown ? static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u) : -1},
        {"phaseKnown", live.stats.phase2MaskPhaseKnown},
        {"phase", live.stats.phase2MaskPhase},
        {"phaseScore", live.stats.phase2MaskPhaseScore},
        {"phaseMacCrcValid", live.stats.phase2MaskPhaseMacCrcValid},
    };
    record["ess"] = p25Phase2EssJson(live.phase2Ess, redactRaw);
    record["stats"] = {
        {"syncs", audio.syncs},
        {"nids", audio.nids},
        {"nidLock", audio.nidLock},
        {"phase2Bursts", live.stats.phase2Bursts},
        {"phase2VoiceCodewords", live.stats.phase2VoiceCodewords},
        {"phase2TargetVoiceCodewords", audio.phase2TargetVoiceCodewords},
        {"phase2OppositeVoiceCodewords", audio.phase2OppositeVoiceCodewords},
        {"phase2FreshStartAbsDibitKnown", audio.phase2FreshStartAbsDibitKnown},
        {"phase2FreshStartAbsDibit", audio.phase2FreshStartAbsDibit},
        {"phase2ContextVoiceCodewords", audio.phase2ContextVoiceCodewords},
        {"phase2ContextSuppressedVoiceCodewords", audio.phase2ContextSuppressedVoiceCodewords},
        {"phase2PendingAmbeFramesQueued", audio.phase2PendingAmbeFramesQueued},
        {"phase2SuperframeBursts", live.stats.phase2SuperframeBursts},
        {"phase2MaskedBursts", live.stats.phase2MaskedBursts},
        {"phase2TargetMaskedBursts", audio.phase2TargetMaskedBursts},
        {"phase2MacPdus", live.stats.phase2MacPdus},
        {"phase2MacCrcValid", live.stats.phase2MacCrcValid},
        {"phase2TargetMacCrcValid", audio.phase2TargetMacCrcValid},
        {"phase2MacNominalCrcValid", live.stats.phase2MacNominalCrcValid},
        {"phase2MacAltKindCrcValid", live.stats.phase2MacAltKindCrcValid},
        {"phase2MacBitSwapCrcValid", live.stats.phase2MacBitSwapCrcValid},
        {"phase2MacSlipCrcValid", live.stats.phase2MacSlipCrcValid},
        {"phase2MacInvertCrcValid", live.stats.phase2MacInvertCrcValid},
        {"phase2MaskParametersKnown", live.stats.phase2MaskParametersKnown},
        {"phase2MaskPhaseKnown", live.stats.phase2MaskPhaseKnown},
        {"phase2MaskPhase", live.stats.phase2MaskPhase},
        {"phase2MaskPhaseScore", live.stats.phase2MaskPhaseScore},
        {"phase2MaskPhaseMacCrcValid", live.stats.phase2MaskPhaseMacCrcValid},
        {"cqpskLockActive", live.stats.cqpskLockActive},
        {"cqpskLockUsed", live.stats.cqpskLockUsed},
        {"cqpskLockUpdated", live.stats.cqpskLockUpdated},
        {"cqpskSymbolPhaseFraction", live.stats.cqpskSymbolPhaseFraction},
        {"cqpskFineCorrectionApplied", live.stats.cqpskFineCorrectionApplied},
        {"cqpskFineRotationRad", live.stats.cqpskFineRotationRad},
        {"cqpskResidualCarrierHz", live.stats.cqpskResidualCarrierHz},
        {"cqpskPhaseErrorRmsRad", live.stats.cqpskPhaseErrorRmsRad},
        {"cqpskFineCorrectionSymbols", live.stats.cqpskFineCorrectionSymbols},
        {"softDecisionSymbols", live.stats.softDecisionSymbols},
        {"softDecisionQuality", live.stats.softDecisionQuality},
        {"softBitLlrMean", live.stats.softBitLlrMean},
        {"softBitLlrMinimum", live.stats.softBitLlrMinimum},
        {"softLowConfidenceSymbols", live.stats.softLowConfidenceSymbols},
        {"phase2IschDecoded", live.stats.phase2IschDecoded},
        {"phase2IschSync", live.stats.phase2IschSync},
        {"bestPhase2SyncErrors", live.stats.bestPhase2SyncErrors},
        {"bestPhase2SyncDibitOffset", live.stats.bestPhase2SyncDibitOffset},
        {"rejectedVoiceCodewords", audio.phase2RejectedVoiceCodewords},
        {"inputQualityRejectedVoiceCodewords", audio.phase2InputQualityRejectedVoiceCodewords},
        {"wrongSlotVoiceCodewords", audio.phase2WrongSlotVoiceCodewords},
        {"trafficTalkgroupMismatchVoiceCodewords", audio.phase2TrafficTalkgroupMismatchVoiceCodewords},
        {"trafficTalkgroupStaleMismatchVoiceCodewords", audio.phase2TrafficTalkgroupStaleMismatchVoiceCodewords},
        {"duplicateSuppressedVoiceCodewords", audio.phase2DuplicateSuppressedVoiceCodewords},
        {"absoluteDuplicateSuppressedVoiceCodewords", audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords},
        {"sequencerSuppressedVoiceCodewords", audio.phase2SequencerSuppressedVoiceCodewords},
        {"contextVoiceCodewords", audio.phase2ContextVoiceCodewords},
        {"contextSuppressedVoiceCodewords", audio.phase2ContextSuppressedVoiceCodewords},
        {"phase2AmbeDecodeAttempts", audio.phase2AmbeDecodeAttempts},
        {"phase2AmbeAcceptedFrames", audio.phase2AmbeAcceptedFrames},
        {"phase2FedToMbelib", audio.phase2FedToMbelib},
        {"phase2EmittedSpeechOrdinalFrames", audio.phase2EmittedSpeechOrdinals.size()},
        {"phase2ExpectedVoiceCodewords", audio.phase2ExpectedVoiceCodewords},
        {"phase2FeedGaps", audio.phase2FeedGaps},
        {"phase2DiagnosticAmbeProbeAttempts", audio.phase2DiagnosticAmbeProbeAttempts},
        {"phase2DiagnosticAmbeProbeAccepted", audio.phase2DiagnosticAmbeProbeAccepted},
        {"ambeAcceptedCanonicalFrames", audio.phase2AmbeAcceptedCanonicalFrames},
        {"ambeAcceptedFallbackFrames", audio.phase2AmbeAcceptedFallbackFrames},
        {"ambeVariantChanges", audio.phase2AmbeVariantChanges},
        {"ambeVariantUnstable", audio.phase2AmbeVariantUnstable},
        {"audioLockMissing", audio.phase2AudioLockMissing},
        {"maskAppliedNoMacCrc", audio.phase2MaskAppliedNoMacCrc},
        {"essMissing", audio.phase2EssMissing},
        {"targetEssKnown", audio.phase2TargetEssKnown},
        {"targetEssEncrypted", audio.phase2TargetEssEncrypted},
        {"wrongSlot", audio.phase2WrongSlot},
        {"ambeRejected", audio.phase2AmbeRejected},
        {"lateEntryWaiting", audio.phase2LateEntryWaiting},
        {"decodedFrames", audio.decodedFrames},
        {"audioSamples", audio.audio.size()},
        {"backendAvailable", audio.backendAvailable},
    };

    if (detailedValidationRecord) {
        record["macPdus"] = json::array();
        P25ControlChannelAnalyzer validationMacAnalyzer;
        for (const auto& pdu : live.phase2MacPdus) {
            json parsedEvents = json::array();
            const auto parsed = validationMacAnalyzer.ingestPhase2MacPdu(
                pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid, pdu.macStructureMaxBits);
            for (const auto& ev : parsed) {
                parsedEvents.push_back({
                    {"type", p25ControlEventTypeToString(ev.type)},
                    {"label", ev.label},
                    {"macMessageOpcode", ev.macMessageOpcode},
                    {"macMessageOffset", ev.macMessageOffset},
                    {"talkgroupId", ev.talkgroupId},
                    {"sourceId", ev.sourceId},
                    {"channel", ev.channel},
                    {"channelB", ev.channelB},
                    {"phase2Candidate", ev.phase2Candidate},
                    {"tdmaSlotKnown", ev.tdmaSlotKnown},
                    {"tdmaSlot", ev.tdmaSlotKnown ? static_cast<int>(ev.tdmaSlot) : -1},
                    {"serviceOptionsKnown", ev.serviceOptionsKnown},
                    {"serviceOptions", ev.serviceOptionsKnown ? static_cast<int>(ev.serviceOptions) : -1},
                    {"encryptionKnown", ev.encryptionKnown},
                    {"encrypted", ev.encrypted},
                });
            }
            record["macPdus"].push_back({
                {"dibitOffset", pdu.dibitOffset},
                {"detectedKind", P25LiveDecoder::phase2BurstKindToString(pdu.detectedKind)},
                {"source", P25LiveDecoder::phase2BurstKindToString(pdu.source)},
                {"opcode", pdu.opcode},
                {"pduTypeName", p25Phase2MacPduTypeToString(pdu.opcode)},
                {"offset", pdu.offset},
                {"fecDecoded", pdu.fecDecoded},
                {"crcValid", pdu.crcValid},
                {"correctedSymbols", pdu.correctedSymbols},
                {"acchHypothesisKnown", pdu.acchHypothesisKnown},
                {"acchBitOrderSwapped", pdu.acchBitOrderSwapped},
                {"acchDibitInverted", pdu.acchDibitInverted},
                {"acchSlipDibits", pdu.acchSlipDibits},
                {"bytesHex", p25BytesToHex(pdu.bytes).toStdString()},
                {"essPresent", pdu.essPresent},
                {"ess", p25Phase2EssJson(pdu.ess, redactRaw)},
                {"parsedEvents", parsedEvents},
            });
        }

        record["bursts"] = json::array();
        for (const auto& burst : live.phase2Bursts) {
            json codewords = json::array();
            for (const auto& cw : burst.voiceCodewords) {
                std::array<uint8_t, 96> ambe = p25Phase2VoiceCodewordToAmbe3600x2450Frame(cw);
                codewords.push_back({
                    {"voiceIndex", cw.voiceIndex},
                    {"dibitOffset", cw.dibitOffset},
                    {"streamDibitKnown", cw.streamDibitKnown},
                    {"streamDibit", cw.streamDibitKnown ? json(cw.streamDibit) : json(nullptr)},
                    {"ambeBits", redactRaw ? std::string("<redacted>") : p25CompactBits(ambe)},
                    {"sessionCodewordIdKnown", cw.sessionCodewordIdKnown},
                    {"sessionCodewordId", cw.sessionCodewordIdKnown ? static_cast<long long>(cw.sessionCodewordId) : -1},
                    {"duplicateInSession", cw.duplicateInSession},
                });
            }
            record["bursts"].push_back({
                {"dibitOffset", burst.dibitOffset},
                {"streamBurstStartDibitKnown", burst.streamBurstStartDibitKnown},
                {"streamBurstStartDibit", burst.streamBurstStartDibitKnown ? json(burst.streamBurstStartDibit) : json(nullptr)},
                {"syncErrors", burst.syncErrors},
                {"superframeLocked", burst.superframeLocked},
                {"superframeDibitOffset", burst.superframeDibitOffset},
                {"superframeSyncScore", burst.superframeSyncScore},
                {"superframeSyncErrors", burst.superframeSyncErrors},
                {"phase2AudioLock", burst.phase2AudioLock},
                {"tdmaSyncLock", burst.tdmaSyncLock},
                {"superframeLock", burst.superframeLock},
                {"maskPhaseLock", burst.maskPhaseLock},
                {"macCrcLock", burst.macCrcLock},
                {"sessionAudioRelease", burst.sessionAudioRelease},
                {"securityStateFromPtt", burst.securityStateFromPtt},
                {"essObservedThisBurst", burst.essObservedThisBurst},
                {"trafficSecurityObservedThisBurst", burst.trafficSecurityObservedThisBurst},
                {"trafficTalkgroupObservedThisBurst", burst.trafficTalkgroupObservedThisBurst},
                {"macPttSeen", burst.macPttSeen},
                {"macEndPttSeen", burst.macEndPttSeen},
                {"macIdleSeen", burst.macIdleSeen},
                {"macHangtimeSeen", burst.macHangtimeSeen},
                {"macActiveSeen", burst.macActiveSeen},
                {"trafficSecurityKnown", burst.trafficSecurityKnown},
                {"trafficEncrypted", burst.trafficEncrypted},
                {"trafficTalkgroupKnown", burst.trafficTalkgroupKnown},
                {"trafficTalkgroupId", burst.trafficTalkgroupId},
                {"superframeBurstIndexKnown", burst.superframeBurstIndexKnown},
                {"superframeBurstIndex", burst.superframeBurstIndex},
                {"grantSlotKnown", burst.grantSlotKnown},
                {"grantSlot", burst.grantSlot},
                {"kind", P25LiveDecoder::phase2BurstKindToString(burst.kind)},
                {"duid", burst.duid},
                {"duidErrors", burst.duidErrors},
                {"xorMaskApplied", burst.xorMaskApplied},
                {"xorMaskPhaseKnown", burst.xorMaskPhaseKnown},
                {"xorMaskPhase", burst.xorMaskPhase},
                {"xorMaskPhaseScore", burst.xorMaskPhaseScore},
                {"macFecDecoded", burst.macFecDecoded},
                {"macCrcValid", burst.macCrcValid},
                {"essKnown", burst.essKnown},
                {"essEncrypted", burst.essEncrypted},
                {"encrypted", burst.encrypted},
                {"ischValid", burst.isch.valid},
                {"ischSync", burst.isch.sync},
                {"ischErrors", burst.isch.errors},
                {"ischChannel", burst.isch.channel},
                {"ischLocation", burst.isch.location},
                {"ischFreeAccess", burst.isch.freeAccess},
                {"ischUltraframeCounter", burst.isch.ultraframeCounter},
                {"rawPayloadDibits", redactRaw ? std::string("<redacted>") : p25CompactDibits(burst.rawPayloadDibits)},
                {"postMaskPayloadDibits", redactRaw ? std::string("<redacted>") : p25CompactDibits(burst.maskedPayloadDibits)},
                {"voiceCodewords", std::move(codewords)},
            });
        }

        record["ambeFrames"] = json::array();
        for (const auto& frame : ambeFrames) {
            json variantProbes = json::array();
            for (const auto& probe : frame.variantProbes) {
                variantProbes.push_back({
                    {"variant", probe.variant},
                    {"status", probe.status},
                    {"errors", probe.errors},
                    {"totalErrors", probe.totalErrors},
                    {"pcmSamples", probe.pcmSamples},
                    {"pcmPeak", probe.pcmPeak},
                    {"pcmRms", probe.pcmRms},
                    {"finite", probe.finite},
                    {"usable", probe.usable},
                    {"score", probe.score},
                });
            }
            record["ambeFrames"].push_back({
                {"burstDibitOffset", frame.burstDibitOffset},
                {"superframeBurstIndexKnown", frame.superframeBurstIndexKnown},
                {"superframeBurstIndex", frame.superframeBurstIndex},
                {"grantSlotKnown", frame.grantSlotKnown},
                {"grantSlot", frame.grantSlot},
                {"voiceIndex", frame.voiceIndex},
                {"haveAbsoluteDibits", frame.haveAbsoluteDibits},
                {"codewordAbsDibit", frame.codewordAbsDibit},
                {"codewordEndAbsDibit", frame.codewordEndAbsDibit},
                {"duplicateInSession", frame.duplicateInSession},
                {"duplicateSuppressed", frame.duplicateSuppressed},
                {"duplicateSuppressedByAbsolute", frame.duplicateSuppressedByAbsolute},
                {"duplicateSuppressedBySequencer", frame.duplicateSuppressedBySequencer},
                {"contextSuppressed", frame.contextSuppressed},
                {"lockedVariantBefore", frame.lockedVariantBefore},
                {"lockedVariantAfter", frame.lockedVariantAfter},
                {"variant", frame.variant},
                {"probeScore", frame.probeScore},
                {"variantProbes", variantProbes},
                {"ambeBits", redactRaw ? std::string("<redacted>") : frame.ambeBits},
                {"status", frame.status},
                {"errors", frame.errors},
                {"totalErrors", frame.totalErrors},
                {"message", frame.message},
                {"pcmPeak", frame.pcmPeak},
                {"pcmRms", frame.pcmRms},
                {"accepted", frame.accepted},
                {"timelineEmitted", frame.timelineEmitted},
                {"inputQualityKnown", frame.inputQualityKnown},
                {"inputSoftDecisionQuality", frame.inputSoftDecisionQuality},
                {"inputSoftDecisionSymbols", frame.inputSoftDecisionSymbols},
                {"inputSoftLowConfidenceSymbols", frame.inputSoftLowConfidenceSymbols},
                {"inputSoftLowConfidenceRatio", frame.inputSoftLowConfidenceRatio},
                {"inputCqpskPhaseErrorRmsRad", frame.inputCqpskPhaseErrorRmsRad},
                {"inputBestPhase2SyncErrors", frame.inputBestPhase2SyncErrors},
                {"inputQualityAccepted", frame.inputQualityAccepted},
                {"inputQualityBlockReason", frame.inputQualityBlockReason},
            });
        }
    } else {
        size_t burstVoiceCodewords = 0;
        size_t maskedBursts = 0;
        size_t superframeBursts = 0;
        size_t macCrcValid = 0;
        bool anyEssKnown = false;
        bool anyEssEncrypted = false;
        for (const auto& burst : live.phase2Bursts) {
            burstVoiceCodewords += burst.voiceCodewords.size();
            if (burst.xorMaskApplied) ++maskedBursts;
            if (burst.superframeLocked || burst.superframeLock) ++superframeBursts;
            if (burst.macCrcValid || burst.macCrcLock) ++macCrcValid;
            anyEssKnown = anyEssKnown || burst.essKnown;
            anyEssEncrypted = anyEssEncrypted || (burst.essKnown && burst.essEncrypted);
        }
        record["compactAutoRecord"] = true;
        record["macPdusSummary"] = {
            {"count", live.phase2MacPdus.size()},
            {"crcValid", live.stats.phase2MacCrcValid},
        };
        record["burstsSummary"] = {
            {"count", live.phase2Bursts.size()},
            {"voiceCodewords", burstVoiceCodewords},
            {"maskedBursts", maskedBursts},
            {"superframeBursts", superframeBursts},
            {"macCrcValid", macCrcValid},
            {"essKnown", anyEssKnown},
            {"essEncrypted", anyEssEncrypted},
        };
        record["ambeFramesSummary"] = {
            {"count", ambeFrames.size()},
            {"accepted", audio.phase2AmbeAcceptedFrames},
            {"attempts", audio.phase2AmbeDecodeAttempts},
            {"inputQualityRejected", audio.phase2InputQualityRejectedVoiceCodewords},
            {"diagnosticProbeAccepted", audio.phase2DiagnosticAmbeProbeAccepted},
            {"diagnosticProbeAttempts", audio.phase2DiagnosticAmbeProbeAttempts},
            {"canonicalAccepted", audio.phase2AmbeAcceptedCanonicalFrames},
            {"fallbackAccepted", audio.phase2AmbeAcceptedFallbackFrames},
            {"variantChanges", audio.phase2AmbeVariantChanges},
        };
    }

    try {
        static std::mutex validationFileMutex;
        std::lock_guard<std::mutex> lk(validationFileMutex);
        const QString path = p25Phase2ValidationPath();
        rotateP25Phase2ValidationLogIfNeeded(path);
        std::ofstream f(path.toStdString(), std::ios::app);
        if (f.is_open()) f << record.dump() << "\n";
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to write P25 Phase 2 validation record: {}", ex.what());
    }
}


void writeP25Phase2AudioOutputTrace(const Receiver& rx,
                                           const P25VoiceAudioBlock& audio,
                                           const char* context,
                                           bool outputMutedForSettle,
                                           bool speakerMayEmit,
                                           bool engineAvailable,
                                           size_t activeOutputCount,
                                           size_t ringQueuedSamples,
                                           double ringFillPercent,
                                           int underrunCount,
                                           size_t outputSamples,
                                           double outputRateHz)
{
    if (!p25Phase2DeepTraceEnabled() || !rx.p25VoicePhase2) return;
    if (audio.phase2Bursts == 0 && audio.phase2VoiceCodewords == 0 && audio.phase2AmbeDecodeAttempts == 0) return;

    json record;
    record["schema"] = "sdr-town-p25-phase2-output-trace-v1";
    record["timeUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    record["context"] = context ? context : "";
    record["talkgroupId"] = rx.p25VoiceTalkgroupId;
    record["receiverFreqHz"] = rx.freqHz;
    record["centerFreqHz"] = audio.centerFreqHz;
    record["effectiveTargetFreqHz"] = audio.effectiveTargetFreqHz;
    record["effectiveTargetOffsetHz"] = audio.effectiveTargetFreqHz - audio.centerFreqHz;
    record["trafficTargetOffsetLock"] = {
        {"known", rx.p25Phase2TrafficTargetOffsetKnown},
        {"offsetHz", rx.p25Phase2TrafficTargetOffsetHz},
        {"trust", rx.p25Phase2TrafficTargetOffsetTrust},
        {"misses", rx.p25Phase2TrafficTargetOffsetMisses},
    };
    record["diag"] = p25VoiceDiagLabel(audio.diag);
    record["phase2"] = {
        {"bursts", audio.phase2Bursts},
        {"voiceCodewords", audio.phase2VoiceCodewords},
        {"targetVoiceCodewords", audio.phase2TargetVoiceCodewords},
        {"oppositeVoiceCodewords", audio.phase2OppositeVoiceCodewords},
        {"freshStartAbsDibitKnown", audio.phase2FreshStartAbsDibitKnown},
        {"freshStartAbsDibit", audio.phase2FreshStartAbsDibit},
        {"contextVoiceCodewords", audio.phase2ContextVoiceCodewords},
        {"contextSuppressedVoiceCodewords", audio.phase2ContextSuppressedVoiceCodewords},
        {"superframeBursts", audio.phase2SuperframeBursts},
        {"maskedBursts", audio.phase2MaskedBursts},
        {"macCrcValid", audio.phase2MacCrcValid},
        {"macPdus", audio.phase2MacPdus},
        {"essKnown", audio.phase2EssKnown},
        {"essEncrypted", audio.phase2EssEncrypted},
        {"ambeAttempts", audio.phase2AmbeDecodeAttempts},
        {"ambeAccepted", audio.phase2AmbeAcceptedFrames},
        {"ambeProbeAttempts", audio.phase2DiagnosticAmbeProbeAttempts},
        {"ambeProbeAccepted", audio.phase2DiagnosticAmbeProbeAccepted},
        {"ambeAcceptedCanonicalFrames", audio.phase2AmbeAcceptedCanonicalFrames},
        {"ambeAcceptedFallbackFrames", audio.phase2AmbeAcceptedFallbackFrames},
        {"ambeVariantChanges", audio.phase2AmbeVariantChanges},
        {"ambeVariantUnstable", audio.phase2AmbeVariantUnstable},
        {"duplicateSuppressedVoiceCodewords", audio.phase2DuplicateSuppressedVoiceCodewords},
        {"absoluteDuplicateSuppressedVoiceCodewords", audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords},
        {"sequencerSuppressedVoiceCodewords", audio.phase2SequencerSuppressedVoiceCodewords},
        {"pendingAmbeFramesQueued", audio.phase2PendingAmbeFramesQueued},
        {"pendingAmbeFramesReleased", audio.phase2PendingAmbeFramesReleased},
        {"decodedFrames", audio.decodedFrames},
        {"backendAvailable", audio.backendAvailable},
    };
    record["securityGate"] = {
        {"action", audio.phase2SecurityGateAction},
        {"preSecurityAudioSamples", audio.phase2PreSecurityAudioSamples},
        {"preSecurityDecodedFrames", audio.phase2PreSecurityDecodedFrames},
        {"postSecurityAudioSamples", audio.audio.size()},
        {"postSecurityDecodedFrames", audio.decodedFrames},
        {"pendingSamplesBefore", audio.phase2PendingAudioSamplesBefore},
        {"pendingSamplesAfter", audio.phase2PendingAudioSamplesAfter},
        {"trustedClear", audio.phase2SecurityTrustedClear},
        {"trustedEncrypted", audio.phase2SecurityTrustedEncrypted},
        {"unknownSecurity", audio.phase2SecurityUnknown},
        {"currentProbePcmUsable", audio.phase2CurrentProbePcmUsable},
        {"fieldAudioProbeAllowed", audio.phase2FieldAudioProbeAllowed},
        {"unknownProbeQualityOk", audio.phase2UnknownProbeQualityOk},
        {"unknownProbeBlockReason", audio.phase2UnknownProbeBlockReason},
        {"targetEssKnown", audio.phase2TargetEssKnown},
        {"targetEssEncrypted", audio.phase2TargetEssEncrypted},
        {"targetMacCrcValid", audio.phase2TargetMacCrcValid},
        {"targetSessionAudioRelease", audio.phase2TargetSessionAudioRelease},
        {"targetSecurityStateFromPtt", audio.phase2TargetSecurityStateFromPtt},
        {"currentFeedTrustedTargetBurst", audio.phase2CurrentFeedTrustedTargetBurst},
        {"sameCallSelectedTimeslotContinuation", audio.phase2SameCallSelectedTimeslotContinuation},
        {"sdrtrunkLateEntryVoiceRelease", audio.phase2SdrtrunkLateEntryVoiceRelease},
        {"explicitClearGrantVoiceRelease", audio.phase2ExplicitClearGrantVoiceRelease},
        {"grantClearKnown", rx.p25VoiceClearKnown},
        {"grantEncrypted", rx.p25VoiceEncrypted},
        {"grantAgeMs", rx.p25VoiceGrantEpochMs > 0
            ? (QDateTime::currentMSecsSinceEpoch() - rx.p25VoiceGrantEpochMs)
            : -1},
    };
    record["speakerGate"] = {
        {"reason", audio.phase2SpeakerGateReason.empty() ? p25VoiceBlockSpeakerGateReason(audio) : audio.phase2SpeakerGateReason},
        {"outputMutedForSettle", outputMutedForSettle},
        {"speakerMayEmit", speakerMayEmit},
        {"engineAvailable", engineAvailable},
        {"activeOutputCount", activeOutputCount},
        {"ringQueuedSamples", ringQueuedSamples},
        {"ringFillPercent", ringFillPercent},
        {"underrunCount", underrunCount},
        {"outputSamples", outputSamples},
        {"outputRateHz", outputRateHz},
        {"audioMetrics", p25AudioVectorMetricsJson(audio.audio)},
    };
    record["flags"] = {
        {"skippedEncrypted", audio.skippedEncrypted},
        {"waitingForClearGrant", audio.waitingForClearGrant},
        {"audioLockMissing", audio.phase2AudioLockMissing},
        {"metadataMissing", audio.phase2MetadataMissing},
        {"maskMissing", audio.phase2MaskMissing},
        {"maskAppliedNoMacCrc", audio.phase2MaskAppliedNoMacCrc},
        {"essMissing", audio.phase2EssMissing},
        {"wrongSlot", audio.phase2WrongSlot},
        {"ambeRejected", audio.phase2AmbeRejected},
        {"ambeVariantUnstable", audio.phase2AmbeVariantUnstable},
        {"lateEntryWaiting", audio.phase2LateEntryWaiting},
        {"voiceUnsupported", audio.phase2VoiceUnsupported},
    };

    try {
        const QString path = p25Phase2ValidationPath();
        rotateP25Phase2ValidationLogIfNeeded(path);
        static std::mutex outputTraceMutex;
        std::lock_guard<std::mutex> lk(outputTraceMutex);
        std::ofstream f(path.toStdString(), std::ios::app);
        if (f.is_open()) f << record.dump() << "\n";
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to write P25 Phase 2 output trace: {}", ex.what());
    }
}


void populateP25VoiceAudioBlockFromLive(P25VoiceAudioBlock& out, const P25LiveDecodeResult& live)
{
    out.decoderRan = true;
    out.syncs = live.syncs.size();
    out.nids = live.nids.size();
    out.nidLock = p25HasValidatedNid(live);
    out.phase2Bursts = live.stats.phase2Bursts;
    out.phase2VoiceCodewords = live.stats.phase2VoiceCodewords;
    out.phase2SuperframeBursts = live.stats.phase2SuperframeBursts;
    out.phase2MaskedBursts = live.stats.phase2MaskedBursts;
    out.phase2MacPdus = live.stats.phase2MacPdus;
    out.phase2MacCrcValid = live.stats.phase2MacCrcValid;
    out.phase2MacFecDecoded = live.stats.phase2MacFecDecoded;
    out.phase2MacDirectCrcValid = live.stats.phase2MacDirectCrcValid;
    out.phase2MacDirectCrcRejected = live.stats.phase2MacDirectCrcRejected;
    out.phase2MacRsDecoded = live.stats.phase2MacRsDecoded;
    out.phase2MacNominalCrcValid = live.stats.phase2MacNominalCrcValid;
    out.phase2MacAltKindCrcValid = live.stats.phase2MacAltKindCrcValid;
    out.phase2MacBitSwapCrcValid = live.stats.phase2MacBitSwapCrcValid;
    out.phase2MacSlipCrcValid = live.stats.phase2MacSlipCrcValid;
    out.phase2MacInvertCrcValid = live.stats.phase2MacInvertCrcValid;
    out.phase2EssKnown = live.stats.phase2EssKnown;
    out.phase2EssEncrypted = live.stats.phase2EssEncrypted;
    out.cqpskLockActive = live.stats.cqpskLockActive;
    out.cqpskLockMisses = live.stats.cqpskLockMisses;
    out.cqpskResidualCarrierHz = live.stats.cqpskResidualCarrierHz;
    out.cqpskPhaseErrorRmsRad = live.stats.cqpskPhaseErrorRmsRad;
    out.dspFramerBurstsEmitted = live.stats.dspFramerBurstsEmitted;
    out.demodState = live.stats.demodState;
    out.dibitCount = live.dibits.size();
    out.demodPath = live.stats.demodPath;
    out.decoderWarnings = live.warnings;
    out.imbeFrames = live.imbeFrames.size();
}


int p25Phase2LiveAudioRecoveryScore(const P25LiveDecodeResult& live)
{
    int score = 0;
    // Hard evidence first.  MAC/ESS is the same layer sdrtrunk uses to drive
    // P25P2AudioModule clear/encrypted state.
    const bool structuredMac =
        live.stats.phase2MacCrcValid > 0 &&
        (live.stats.phase2MaskedBursts > 0 ||
         live.stats.phase2SuperframeBursts > 0 ||
         live.stats.phase2IschDecoded > 0);
    score += static_cast<int>(live.stats.phase2MacCrcValid) * (structuredMac ? 100000 : 4000);
    if (live.stats.phase2EssKnown) score += (structuredMac ? 80000 : 12000);
    // FEC-decoded/non-CRC MAC candidates prove we reached ACCH extraction/RS
    // and are much more useful than voice-looking DUID telemetry.
    const size_t fecMac = static_cast<size_t>(std::count_if(
        live.phase2MacPdus.begin(), live.phase2MacPdus.end(),
        [](const P25Phase2MacPdu& pdu) { return pdu.fecDecoded; }));
    score += static_cast<int>(fecMac) * 4000;
    score += static_cast<int>(live.stats.phase2MacPdus) * 800;
    score += static_cast<int>(live.stats.phase2MaskedBursts) * 80;
    score += static_cast<int>(live.stats.phase2SuperframeBursts) * 70;
    score += static_cast<int>(live.stats.phase2VoiceCodewords) * 30;
    score += static_cast<int>(live.stats.phase2IschDecoded) * 20;
    if (live.stats.phase2MaskPhaseKnown) score += 120;
    if (live.stats.bestPhase2SyncErrors >= 0) score += std::max(0, 10 - live.stats.bestPhase2SyncErrors) * 10;
    score += static_cast<int>(std::clamp(live.stats.softDecisionQuality, 0.0, 1.0) * 25.0);
    return score;
}


bool p25Phase2NeedsTargetOffsetProbe(const P25LiveDecodeResult& live)
{
    // Only ask for a bounded offset probe when the first pass sees some TDMA
    // voice evidence but cannot lock the superframe/mask/MAC layer.  A lone
    // ACCH CRC without superframe/mask/ISCH structure is not enough to suppress
    // probing; field logs showed those weak hits pulling the retuned one-RTL
    // traffic channel a few kHz away from the only offset that produced AMBE.
    // Cold zero-burst one-RTL acquire is handled separately via
    // acquisitionOffsetProbe so quiet first passes can still probe nearby eyes.
    const bool hardLock =
        live.stats.phase2MaskedBursts > 0 ||
        (live.stats.phase2MacCrcValid > 0 &&
         (live.stats.phase2SuperframeBursts > 0 ||
          live.stats.phase2IschDecoded > 0 ||
          live.stats.phase2MaskPhaseKnown));
    if (hardLock) return false;
    return live.stats.phase2VoiceCodewords > 0 ||
           live.stats.phase2Bursts > 0 ||
           live.stats.phase2MacPdus > 0 ||
           live.stats.phase2MacCrcValid > 0 ||
           live.stats.bestPhase2SyncErrors >= 0;
}


void p25AddUniqueTargetCandidate(std::vector<double>& targets, double hz)
{
    if (!std::isfinite(hz) || hz <= 0.0) return;
    for (double existing : targets) {
        if (std::abs(existing - hz) < 50.0) return;
    }
    targets.push_back(hz);
}


bool p25Phase2TargetInSamplePassband(double sampleRateHz, double centerFreqHz, double targetFreqHz) noexcept
{
    if (!std::isfinite(targetFreqHz) || targetFreqHz <= 0.0) return false;
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0 || !std::isfinite(centerFreqHz)) return true;
    return std::abs(targetFreqHz - centerFreqHz) <= sampleRateHz * 0.47;
}



bool p25Phase2TrafficTalkgroupAuthoritativeThisBurst(const P25Phase2Burst& burst) noexcept
{
    return burst.trafficTalkgroupKnown && burst.trafficTalkgroupObservedThisBurst;
}


bool p25Phase2TrafficTalkgroupBelongsToFollowedCall(
    const Receiver& rx,
    const P25Phase2Burst& burst) noexcept
{
    return !burst.trafficTalkgroupKnown ||
        rx.p25VoiceTalkgroupId == 0 ||
        burst.trafficTalkgroupId == rx.p25VoiceTalkgroupId;
}


bool p25Phase2TrafficTalkgroupMismatchBlocksSelectedSlot(
    const Receiver& rx,
    const P25Phase2Burst& burst) noexcept
{
    return p25Phase2TrafficTalkgroupAuthoritativeThisBurst(burst) &&
        !p25Phase2TrafficTalkgroupBelongsToFollowedCall(rx, burst);
}


bool p25Phase2TrafficTalkgroupKnownMismatch(
    const Receiver& rx,
    const P25Phase2Burst& burst) noexcept
{
    return burst.trafficTalkgroupKnown &&
        rx.p25VoiceTalkgroupId != 0 &&
        burst.trafficTalkgroupId != rx.p25VoiceTalkgroupId;
}


bool p25Phase2BurstEncryptedForFollowedCall(
    const Receiver& rx,
    const P25Phase2Burst& burst) noexcept
{
    if (burst.essKnown && burst.essEncrypted) return true;
    return burst.trafficSecurityKnown &&
        burst.trafficEncrypted &&
        p25Phase2TrafficTalkgroupBelongsToFollowedCall(rx, burst);
}


P25Phase2FollowedSlotEvidence p25Phase2FollowedSlotEvidenceForReceiver(
    const Receiver& rx,
    const P25LiveDecodeResult& live) noexcept
{
    P25Phase2FollowedSlotEvidence ev;
    ev.slotKnown = rx.p25VoiceTdmaSlotKnown;
    if (!ev.slotKnown) {
        ev.targetBursts = live.stats.phase2Bursts;
        ev.targetVoiceCodewords = live.stats.phase2VoiceCodewords;
        ev.targetMaskedBursts = live.stats.phase2MaskedBursts;
        ev.targetSuperframeBursts = live.stats.phase2SuperframeBursts;
        ev.targetMacPdus = live.stats.phase2MacPdus;
        ev.targetMacCrcValid = live.stats.phase2MacCrcValid;
        ev.targetIschDecoded = live.stats.phase2IschDecoded;
        ev.targetEssKnown = live.stats.phase2EssKnown;
        ev.targetEssEncrypted = live.stats.phase2EssEncrypted;
        return ev;
    }

    const uint8_t followedSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
    for (const auto& burst : live.phase2Bursts) {
        const bool trafficTalkgroupBelongs =
            p25Phase2TrafficTalkgroupBelongsToFollowedCall(rx, burst);
        const bool trafficTalkgroupBlocksSelectedSlot =
            p25Phase2TrafficTalkgroupMismatchBlocksSelectedSlot(rx, burst);
        const bool targetSessionAudioRelease =
            burst.sessionAudioRelease && trafficTalkgroupBelongs;
        const bool unlockedSlot0Evidence =
            !burst.grantSlotKnown &&
            followedSlot == 0u &&
            (burst.essKnown || targetSessionAudioRelease || burst.securityStateFromPtt);
        const bool unlockedSlot1Evidence =
            !burst.grantSlotKnown &&
            followedSlot == 1u &&
            (burst.essKnown || targetSessionAudioRelease || burst.securityStateFromPtt || burst.macPttSeen || burst.macActiveSeen);
        bool targetSlot = burst.grantSlotKnown
            ? static_cast<uint8_t>(burst.grantSlot & 0x01u) == followedSlot
            : (unlockedSlot0Evidence || unlockedSlot1Evidence);
        if (!burst.grantSlotKnown && !(unlockedSlot0Evidence || unlockedSlot1Evidence)) {
            // Without a decoded slot/ESS/PTT association this burst is useful
            // RF evidence, but it is not target-slot proof.  Keep it out of the
            // followed-slot counters so opposite-slot AMBE cannot release audio.
            continue;
        }
        if (!targetSlot) {
            ev.oppositeVoiceCodewords += burst.voiceCodewords.size();
            continue;
        }
        if (trafficTalkgroupBlocksSelectedSlot) {
            ev.oppositeVoiceCodewords += burst.voiceCodewords.size();
            continue;
        }

        ++ev.targetBursts;
        ev.targetVoiceCodewords += burst.voiceCodewords.size();
        if (burst.xorMaskApplied) ++ev.targetMaskedBursts;
        if (burst.superframeLocked || burst.superframeLock) ++ev.targetSuperframeBursts;
        if (burst.macCrcValid || burst.macCrcLock) ++ev.targetMacCrcValid;
        if (burst.macCrcValid || burst.macCrcLock || !burst.voiceCodewords.empty()) ++ev.targetMacPdus;
        if (burst.isch.valid) ++ev.targetIschDecoded;
        if (burst.essKnown) {
            ev.targetEssKnown = true;
            ev.targetEssEncrypted = ev.targetEssEncrypted || burst.essEncrypted;
        }
        if (trafficTalkgroupBelongs && burst.trafficSecurityKnown) {
            ev.targetEssKnown = true;
            ev.targetEssEncrypted = ev.targetEssEncrypted || burst.trafficEncrypted;
        }
        ev.targetSessionAudioRelease = ev.targetSessionAudioRelease || targetSessionAudioRelease;
    }
    return ev;
}


bool p25Phase2TrafficTargetOffsetEvidenceStrong(const P25LiveDecodeResult& live) noexcept
{
    const bool structuredMac =
        live.stats.phase2MacCrcValid > 0 &&
        (live.stats.phase2MaskedBursts > 0 ||
         live.stats.phase2SuperframeBursts > 0 ||
         live.stats.phase2IschDecoded > 0);
    const bool superframeMaskLock =
        live.stats.phase2SuperframeBursts >= 6 &&
        live.stats.phase2MaskedBursts >= 6;
    const bool descrambledVoice =
        live.stats.phase2VoiceCodewords > 0 &&
        live.stats.phase2MaskedBursts > 0 &&
        (live.stats.phase2SuperframeBursts > 0 ||
         live.stats.phase2IschDecoded > 0 ||
         live.stats.phase2MaskPhaseKnown);
    return structuredMac || superframeMaskLock || descrambledVoice;
}


bool p25Phase2TrafficTargetOffsetEvidenceStrong(const Receiver& rx,
                                                       const P25LiveDecodeResult& live) noexcept
{
    const auto ev = p25Phase2FollowedSlotEvidenceForReceiver(rx, live);
    if (!ev.slotKnown) return p25Phase2TrafficTargetOffsetEvidenceStrong(live);

    const bool structuredTargetMac =
        ev.targetMacCrcValid > 0 &&
        (ev.targetMaskedBursts > 0 ||
         ev.targetSuperframeBursts > 0 ||
         ev.targetIschDecoded > 0 ||
         ev.targetEssKnown);
    const bool targetEssOrSession = ev.targetEssKnown || ev.targetSessionAudioRelease;
    const bool strongTargetVoiceEye =
        ev.targetVoiceCodewords >= 6 &&
        ev.targetMaskedBursts >= 6 &&
        ev.targetSuperframeBursts >= 6 &&
        (ev.targetIschDecoded > 0 || structuredTargetMac || targetEssOrSession);
    const bool activeFollowedSlotVoiceEye =
        // We are already on the granted RF and the followed slot is producing
        // descrambled VCWs under a stable superframe/mask cadence.  Do not keep
        // burning CPU on retune-offset probes just because MAC/ESS is sparse;
        // P25 Phase 2 voice-only windows commonly have p2mac=0/12.
        ev.targetVoiceCodewords >= 2 &&
        ev.targetMaskedBursts >= 2 &&
        ev.targetSuperframeBursts >= 6;

    // Do not let a few isolated 2V/4V detections lock the one-RTL traffic
    // channelizer a kHz or two away from the granted center.  The field log
    // showed target=421.97625 MHz on a 421.97500 MHz grant immediately before
    // no-voice-sync windows.  Only MAC/ESS/session proof, or a strong followed-
    // slot superframe/mask/ISCH eye, is stable enough to persist an offset.
    return structuredTargetMac || targetEssOrSession || strongTargetVoiceEye || activeFollowedSlotVoiceEye;
}


int p25Phase2LiveAudioRecoveryScore(const Receiver& rx,
                                           const P25LiveDecodeResult& live)
{
    int score = p25Phase2LiveAudioRecoveryScore(live);
    const auto ev = p25Phase2FollowedSlotEvidenceForReceiver(rx, live);
    if (!ev.slotKnown) return score;

    score += static_cast<int>(ev.targetVoiceCodewords) * 160;
    score += static_cast<int>(ev.targetMaskedBursts) * 140;
    score += static_cast<int>(ev.targetSuperframeBursts) * 120;
    score += static_cast<int>(ev.targetIschDecoded) * 80;
    score += static_cast<int>(ev.targetMacCrcValid) * 12000;
    if (ev.targetEssKnown) score += ev.targetEssEncrypted ? 30000 : 90000;
    if (ev.targetSessionAudioRelease) score += 110000;
    if (rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted && ev.targetEssKnown && ev.targetEssEncrypted) {
        score -= 200000;
    }
    if (ev.targetVoiceCodewords == 0 && ev.oppositeVoiceCodewords > 0) {
        score -= static_cast<int>(std::min<size_t>(ev.oppositeVoiceCodewords, 16)) * 2500;
    }
    return score;
}


bool p25Phase2LiveHasRetuneProbeTelemetry(const P25LiveDecodeResult& live) noexcept
{
    return live.stats.phase2VoiceCodewords > 0 ||
           live.stats.phase2Bursts > 0 ||
           live.stats.phase2MacPdus > 0 ||
           live.stats.phase2MacCrcValid > 0 ||
           live.stats.phase2IschDecoded > 0 ||
           live.stats.bestPhase2SyncErrors >= 0;
}


void p25Phase2UpdateTrafficTargetOffsetLock(Receiver& rx,
                                                   const P25LiveDecodeResult& live,
                                                   double nominalTargetFreqHz,
                                                   double effectiveTargetFreqHz)
{
    if (!kP25Phase2TrafficTargetOffsetProbeEnabled) return;
    if (!rx.p25VoicePhase2 || !rx.p25TrafficRetunesPrimary) return;
    if (!std::isfinite(nominalTargetFreqHz) || nominalTargetFreqHz <= 0.0 ||
        !std::isfinite(effectiveTargetFreqHz) || effectiveTargetFreqHz <= 0.0) {
        return;
    }

    if (!p25Phase2TrafficTargetOffsetEvidenceStrong(rx, live)) {
        if (rx.p25Phase2TrafficTargetOffsetKnown) {
            // Unverified AFC/control-channel seeds are not demoted on quiet
            // windows; only verified traffic offsets age out when telemetry stops.
            if (rx.p25Phase2TrafficTargetOffsetTrust < kP25Phase2TrafficTargetOffsetVerifiedTrust) {
                return;
            }
            rx.p25Phase2TrafficTargetOffsetMisses = std::min(rx.p25Phase2TrafficTargetOffsetMisses + 1, 1000);
            // A verified one-RTL traffic offset is only useful while it keeps
            // producing real TDMA structure.  If it goes quiet, demote it back
            // to an unverified probe candidate so the next windows start from
            // the granted channel center and can recover from a stale/bad lock.
            const bool noPhase2Telemetry = !p25Phase2LiveHasRetuneProbeTelemetry(live) ||
                (live.stats.phase2Bursts == 0 && live.stats.phase2VoiceCodewords == 0 &&
                 live.stats.phase2MacPdus == 0 && live.stats.phase2IschDecoded == 0);
            if (rx.p25Phase2TrafficTargetOffsetTrust >= kP25Phase2TrafficTargetOffsetVerifiedTrust &&
                rx.p25Phase2TrafficTargetOffsetMisses >= 2) {
                rx.p25Phase2TrafficTargetOffsetTrust = kP25Phase2TrafficTargetOffsetVerifiedTrust - 1;
            }
            if (rx.p25Phase2TrafficTargetOffsetTrust <= 0 ||
                rx.p25Phase2TrafficTargetOffsetMisses >= 5 ||
                (noPhase2Telemetry && rx.p25Phase2TrafficTargetOffsetMisses >= 2)) {
                rx.p25Phase2TrafficTargetOffsetKnown = false;
                rx.p25Phase2TrafficTargetOffsetHz = 0.0;
                rx.p25Phase2TrafficTargetOffsetTrust = 0;
                rx.p25Phase2TrafficTargetOffsetMisses = 0;
            }
        }
        return;
    }

    const double offsetHz = effectiveTargetFreqHz - nominalTargetFreqHz;
    if (!std::isfinite(offsetHz)) return;
    const double maxOffsetHz = rx.p25TrafficRetunesPrimary
        ? kP25Phase2TrafficTargetOffsetMaxHz
        : 45000.0;
    if (std::abs(offsetHz) > maxOffsetHz) {
        if (rx.p25Phase2TrafficTargetOffsetKnown) {
            p25Phase2ResetTrafficTargetOffset(rx);
        }
        return;
    }
    if (std::abs(offsetHz) < 50.0) return;

    rx.p25Phase2TrafficTargetOffsetKnown = true;
    rx.p25Phase2TrafficTargetOffsetHz = offsetHz;
    rx.p25Phase2TrafficTargetOffsetTrust = std::min(rx.p25Phase2TrafficTargetOffsetTrust + 1, 1000);
    rx.p25Phase2TrafficTargetOffsetMisses = 0;
}


// Companion TDMA timeslot observe/decode (SDRTrunk second P25P2AudioModule).
// Advances the opposite AMBE vocoder and optional pending queue for
// priority/multi-record. NEVER inserts PCM into out.audio — selected speaker
// isolation stays hard (no opposite-slot invent-PLC / mix).
void p25Phase2ObserveOppositeSlotAmbe(Receiver& rx,
                                             const P25Phase2Burst& burst,
                                             uint8_t oppositeSlot,
                                             double targetFreqHz,
                                             P25VoiceAudioBlock& out)
{
    if (!burst.xorMaskApplied || burst.encrypted || burst.voiceCodewords.empty()) {
        return;
    }
    P25P2CallAudioKey oppKey;
    oppKey.nac = rx.p25VoiceNac;
    oppKey.wacn = rx.p25VoiceWacn;
    oppKey.systemId = rx.p25VoiceSystemId;
    oppKey.talkgroupId = (burst.trafficTalkgroupKnown && burst.trafficTalkgroupId != 0)
        ? burst.trafficTalkgroupId
        // Synthetic observe TG when companion identity is unknown so pending can
        // still arm for multi-record (never used as selected speaker key).
        : (0x7F000000u | static_cast<uint32_t>(oppositeSlot & 0x01u));
    oppKey.sourceId = 0;
    oppKey.callSessionId = rx.p25CurrentCallSessionId;
    oppKey.grantEpochMs = rx.p25VoiceGrantEpochMs;
    oppKey.slot = oppositeSlot;
    oppKey.frequencyHz = p25RoundFrequencyHz(targetFreqHz);
    const bool canQueue = oppKey.valid();

    for (const auto& codeword : burst.voiceCodewords) {
        ++out.phase2OppositeAmbeDecodeAttempts;
        const auto ambeFrame = p25Phase2VoiceCodewordToAmbe3600x2450Frame(codeword);
        const auto decoded = rx.p25AmbeVoiceDecoderOpposite.decodeAmbe3600x2450Frame(ambeFrame);
        if (p25AmbeDecodeFrameLooksUsable(decoded) &&
            p25DecodedAmbePcmLooksSafeForSpeaker(decoded)) {
            ++out.phase2OppositeAmbeAcceptedFrames;
            // Multi-record path: resample companion PCM with its own resampler.
            // NEVER insert into out.audio (selected speaker isolation).
            auto recordBlock = resampleDecodedP25PcmWithState(
                rx.p25SessionState.resamplerOpposite,
                decoded.pcm,
                decoded.sampleRate,
                48000.0);
            if (!recordBlock.empty()) {
                out.phase2OppositeRecordPcm.insert(
                    out.phase2OppositeRecordPcm.end(),
                    recordBlock.begin(),
                    recordBlock.end());
                out.phase2OppositeRecordSamples += recordBlock.size();
                appendCliP25OppositeWavCapture(recordBlock);
            }
        }
        if (!canQueue) continue;

        P25P2PendingAmbeFrame pending;
        pending.ambe96 = ambeFrame;
        pending.voiceIndex = codeword.voiceIndex;
        pending.grantSlotKnown = true;
        pending.grantSlot = oppositeSlot;
        pending.haveAbsoluteDibits = codeword.streamDibitKnown;
        pending.codewordAbsDibit = codeword.streamDibit;
        pending.codewordEndAbsDibit = codeword.streamDibitKnown
            ? (codeword.streamDibit + 36u)
            : 0u;

        auto& queue = rx.p25SessionState.pendingAudioOpposite;
        if (!queue.armed || !(queue.key == oppKey)) {
            queue = P25P2PendingAudioQueue{};
            queue.key = oppKey;
            queue.armed = true;
        }
        if (pending.haveAbsoluteDibits) {
            constexpr uint64_t kOppPendingDupTol = 12u;
            bool dup = false;
            for (const auto& existing : queue.ambeFrames) {
                if (!existing.haveAbsoluteDibits) continue;
                const uint64_t delta = pending.codewordAbsDibit > existing.codewordAbsDibit
                    ? pending.codewordAbsDibit - existing.codewordAbsDibit
                    : existing.codewordAbsDibit - pending.codewordAbsDibit;
                if (delta <= kOppPendingDupTol) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
        }
        queue.ambeFrames.push_back(pending);
        while (queue.ambeFrames.size() > kP25Phase2PendingQueueMaxFrames) {
            queue.ambeFrames.pop_front();
        }
        ++out.phase2OppositePendingQueued;
    }
}


P25VoiceAudioBlock decodeP25Phase2VoiceBlock(Receiver& rx,
                                                    const P25LiveDecodeResult& live,
                                                    P25VoiceAudioBlock out,
                                                    double sampleRateHz,
                                                    double centerFreqHz,
                                                    double targetFreqHz,
                                                    double outputRateHz,
                                                    uint64_t windowStartAbsDibit,
                                                    bool haveAbsoluteDibits,
                                                    uint64_t freshStartAbsDibit,
                                                    bool haveFreshStartDibits)
{
    std::vector<P25Phase2AmbeValidationFrame> ambeFrames;
    P25AmbeVoiceDecoder diagnosticAmbeDecoder;
    bool sawVoice = false;
    bool acceptedVoice = false;
    bool acceptedReleaseVoice = false;
    bool queuedRawVoice = false;
    bool drainedPendingRawVoice = false;
    bool skippedDuplicateVoice = false;
    bool attemptedNewVoice = false;
    bool lateEntryStrongTargetReleaseDecoded = false;
    bool currentWindowHasFeedTrustedTargetBurst = false;
    const bool writePreGateValidation = p25Phase2ValidationLoggingEnabled();
    const P25P2CallAudioKey audioKey = p25CurrentPhase2AudioKey(rx, targetFreqHz);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    out.phase2FreshStartAbsDibitKnown = haveFreshStartDibits;
    out.phase2FreshStartAbsDibit = haveFreshStartDibits ? freshStartAbsDibit : 0;
    // One sustain hop (80 ms) at 6000 dibits/s. Deeper lookback is lock-only
    // after this call has already emitted (DEC-0010). The last hop of overlap
    // still uses ShouldEmit so a missed 80 ms eye can catch up (DEC-0007).
    constexpr uint64_t kFreshContextAudioGraceDibits = 480u;
    const uint64_t contextAudioFloorDibit =
        haveFreshStartDibits && freshStartAbsDibit > kFreshContextAudioGraceDibits
            ? freshStartAbsDibit - kFreshContextAudioGraceDibits
            : 0;
    auto codewordAbsoluteDibitKnown = [&](const P25Phase2VoiceCodeword& codeword) noexcept {
        return codeword.streamDibitKnown || haveAbsoluteDibits;
    };
    auto codewordAbsoluteDibit = [&](const P25Phase2VoiceCodeword& codeword) noexcept -> uint64_t {
        if (codeword.streamDibitKnown) {
            return codeword.streamDibit;
        }
        return haveAbsoluteDibits
            ? windowStartAbsDibit + static_cast<uint64_t>(codeword.dibitOffset)
            : 0u;
    };
    auto codewordBeforeFresh = [&](bool codewordAbsKnown, uint64_t codewordEndAbsDibit) noexcept {
        return codewordAbsKnown && haveFreshStartDibits && codewordEndAbsDibit <= freshStartAbsDibit;
    };
    auto codewordIsContextOnly = [&](bool codewordAbsKnown, uint64_t codewordEndAbsDibit) noexcept {
        return codewordAbsKnown && haveFreshStartDibits && codewordEndAbsDibit <= contextAudioFloorDibit;
    };
    if (!p25Phase2RecentSecurityEvidenceUsable(rx, audioKey, nowMs) &&
        rx.p25Phase2RecentSecurityTalkgroupId != 0) {
        p25ClearPhase2RecentSecurityEvidence(rx);
    }
    const bool latchedClearForCall =
        rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear &&
        !rx.p25VoiceEncrypted;
    const bool explicitClearGrantForCall =
        (rx.p25VoiceClearKnown || latchedClearForCall) && !rx.p25VoiceEncrypted;
    const bool recentClearSecurityForCall =
        p25Phase2RecentSecurityEvidenceUsable(rx, audioKey, nowMs) &&
        (rx.p25Phase2RecentTargetSessionAudioRelease ||
         (rx.p25Phase2RecentTargetEssKnown && !rx.p25Phase2RecentTargetEssEncrypted));
    // Once target traffic proves Clear, preserve it only through the exact
    // same-call recent security evidence.  A broad Clear latch alone must not
    // feed mbelib; doing so can poison the vocoder with unknown-slot bursts.
    const bool establishedClearCall =
        recentClearSecurityForCall &&
        rx.p25VoiceMaskParamsKnown &&
        rx.p25VoiceTdmaSlotKnown;

    bool selectedSlotHasVoiceCodewords = false;
    bool oppositeSlotHasVoiceCodewords = false;
    bool phase2InvertSlotLabelsForWindow = false;
    bool bothSlotsHaveVoiceInWindow = false;
    bool selectedSlotKnownOtherTalkgroupInWindow = false;
    if (rx.p25VoiceTdmaSlotKnown) {
        const uint8_t followedGrantSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);

        // SDRTrunk/OP25 bind one decoder/audio module to the granted TDMA slot
        // and reject the other slot.  Opposite-only windows are useful evidence
        // of a framer epoch problem, but flipping the speaker slot here can feed
        // the other caller into this vocoder state and produces the field symptom
        // of mixed/blocky Phase-2 audio.  Keep the grant slot authoritative.
        size_t normalTargetVoiceCodewords = 0;
        size_t normalOppositeVoiceCodewords = 0;
        size_t slot0Vcw = 0;
        size_t slot1Vcw = 0;
        size_t slot0Mac = 0;
        size_t slot1Mac = 0;
        for (const auto& burst : live.phase2Bursts) {
            if (!burst.grantSlotKnown) continue;
            const uint8_t phys = static_cast<uint8_t>(burst.grantSlot & 0x01u);
            const size_t vcw = burst.voiceCodewords.size();
            if (phys == 0) {
                slot0Vcw += vcw;
                if (burst.macCrcValid || burst.macCrcLock) ++slot0Mac;
            } else {
                slot1Vcw += vcw;
                if (burst.macCrcValid || burst.macCrcLock) ++slot1Mac;
            }
            if (vcw == 0) continue;
            if (phys == followedGrantSlot) {
                normalTargetVoiceCodewords += vcw;
            } else {
                normalOppositeVoiceCodewords += vcw;
            }
        }
        out.phase2Slot0VoiceCodewords = slot0Vcw;
        out.phase2Slot1VoiceCodewords = slot1Vcw;
        out.phase2Slot0MacCrcValid = slot0Mac;
        out.phase2Slot1MacCrcValid = slot1Mac;
        // Dual-slot Phase-2 carriers routinely carry two different TGs at once
        // (field 20260712_124209: TG30003 slot1 + TG10132 slot0 on 420.725).
        // SDRTrunk never "swaps slots" — it binds two independent audio modules
        // to TIMESLOT_1 / TIMESLOT_2 from SuperFrameFragment and only plays the
        // granted timeslot.  Any invert while both slots have voice mixes the
        // other call into ours → long scrambled audio.
        const bool bothSlotsHaveVoice =
            normalTargetVoiceCodewords > 0 && normalOppositeVoiceCodewords > 0;
        bothSlotsHaveVoiceInWindow = bothSlotsHaveVoice;
        // Sticky invert / opposite-only auto-relabel is disabled on the live
        // emit path.  Dual-slot carriers are normal; SDRTrunk never flips the
        // granted timeslot mid-call.
        (void)normalOppositeVoiceCodewords;
        rx.p25Phase2OppositeOnlyWindows = 0;
        if (rx.p25Phase2StickySlotLabelInvert) {
            ++rx.p25DiagStickyInvert;
            rx.p25Phase2StickySlotLabelInvert = false;
        }
        phase2InvertSlotLabelsForWindow = false;
        if (rx.p25VoiceTdmaSlotKnown &&
            (rx.p25VoiceClearKnown || rx.p25VoiceEncrypted || rx.p25VoiceMaskParamsKnown)) {
            p25Phase2MarkGrantedSlotImmutable(rx);
        }

        auto burstIsFollowedSlot = [&](const P25Phase2Burst& burst) noexcept {
            if (!burst.grantSlotKnown) return false;
            uint8_t slot = static_cast<uint8_t>(burst.grantSlot & 0x01u);
            if (phase2InvertSlotLabelsForWindow) slot ^= 0x01u;
            return slot == followedGrantSlot;
        };
        const bool earlyRecentMacEvidenceForCall =
            p25Phase2RecentSecurityEvidenceUsable(rx, audioKey, nowMs) &&
            (rx.p25Phase2RecentTargetMacCrcValid || rx.p25Phase2RecentAnyMacCrcValid);

        for (const auto& burst : live.phase2Bursts) {
            if (!burst.grantSlotKnown ||
                !burst.trafficTalkgroupKnown ||
                rx.p25VoiceTalkgroupId == 0) {
                continue;
            }
            if (!burstIsFollowedSlot(burst)) {
                continue;
            }
            if (burst.trafficTalkgroupId != rx.p25VoiceTalkgroupId) {
                selectedSlotKnownOtherTalkgroupInWindow = true;
            }
        }

        for (const auto& burst : live.phase2Bursts) {
            const bool trafficTalkgroupBelongs =
                p25Phase2TrafficTalkgroupBelongsToFollowedCall(rx, burst);
            const bool trafficTalkgroupKnownMismatch =
                p25Phase2TrafficTalkgroupKnownMismatch(rx, burst);
            const bool trafficTalkgroupBlocksSelectedSlot =
                p25Phase2TrafficTalkgroupMismatchBlocksSelectedSlot(rx, burst);
            const bool burstEncryptedForFollowedCall =
                p25Phase2BurstEncryptedForFollowedCall(rx, burst);
            const bool targetSessionAudioRelease =
                burst.sessionAudioRelease && trafficTalkgroupBelongs;
            // When the Phase-2 live decoder has not rebuilt a superframe index
            // yet, grantSlotKnown is false.  Its unlocked/late-entry fallback
            // session is slot 0, so clear/encrypted ESS from that path is still
            // target-slot evidence for a slot-0 follow.  For slot 1, use symmetric
            // evidence from ess/session/ptt/mac to allow late entry without super
            // (to match SDRTrunk per-timeslot binding from grant).
            const bool unlockedSlot0Evidence =
                !burst.grantSlotKnown &&
                followedGrantSlot == 0u &&
                (burst.essKnown || targetSessionAudioRelease || burst.securityStateFromPtt);
            const bool unlockedSlot1Evidence =
                !burst.grantSlotKnown &&
                followedGrantSlot == 1u &&
                (burst.essKnown || targetSessionAudioRelease || burst.securityStateFromPtt || burst.macPttSeen || burst.macActiveSeen);
            bool targetSlot = burst.grantSlotKnown
                ? burstIsFollowedSlot(burst)
                : (unlockedSlot0Evidence || unlockedSlot1Evidence);
            if (!burst.grantSlotKnown && !(unlockedSlot0Evidence || unlockedSlot1Evidence)) {
                // For established clear calls (proof already obtained), accept descrambled
                // masked bursts even if this window's burst lacks explicit grantSlotKnown.
                // This keeps audio continuous instead of dropping frames between MAC/ESS sightings.
                if (establishedClearCall &&
                    !bothSlotsHaveVoice &&
                    !oppositeSlotHasVoiceCodewords &&
                    burst.xorMaskApplied &&
                    !burstEncryptedForFollowedCall &&
                    !burst.voiceCodewords.empty() &&
                    (burst.superframeLock || burst.macCrcLock || burst.macCrcValid ||
                     out.phase2TargetMacCrcValid || earlyRecentMacEvidenceForCall ||
                     p25Phase2AudioTailGraceActive(rx))) {
                    targetSlot = true;
                } else {
                    // Do not count unlocked descrambled VCWs as target-slot voice.
                    // A Phase-2 RF carrier contains both slots; target ownership must
                    // come from the grant slot, decoded ISCH/superframe slot, or
                    // target-slot security/MAC evidence.
                    continue;
                }
            }
            const bool selectedSlotBlockedByWindowTalkgroup =
                targetSlot &&
                selectedSlotKnownOtherTalkgroupInWindow &&
                (!burst.trafficTalkgroupKnown ||
                 burst.trafficTalkgroupId != rx.p25VoiceTalkgroupId);
            if (targetSlot &&
                (trafficTalkgroupKnownMismatch || selectedSlotBlockedByWindowTalkgroup)) {
                if (!burst.voiceCodewords.empty()) {
                    // A retained or same-window traffic TG label that names a
                    // different TG is still not our audio module.  Count that
                    // selected-slot material as non-target structure and let
                    // the feed loop record the exact reject reason once.
                    oppositeSlotHasVoiceCodewords = true;
                    out.phase2OppositeVoiceCodewords += burst.voiceCodewords.size();
                }
                continue;
            }
            if (targetSlot && trafficTalkgroupBlocksSelectedSlot) {
                if (!burst.voiceCodewords.empty()) {
                    oppositeSlotHasVoiceCodewords = true;
                    out.phase2OppositeVoiceCodewords += burst.voiceCodewords.size();
                    out.phase2TrafficTalkgroupMismatchVoiceCodewords += burst.voiceCodewords.size();
                }
                continue;
            }
            if (targetSlot) {
                out.phase2TargetMacCrcValid = out.phase2TargetMacCrcValid || burst.macCrcValid || burst.macCrcLock;
                // This-window MAC: this-burst FEC/CRC only. After 080304 sticky
                // ptt→macCrcLock fix, macCrcLock is no longer session-painted.
                out.phase2ThisWindowTargetMacCrcValid =
                    out.phase2ThisWindowTargetMacCrcValid || burst.macCrcValid || burst.macCrcLock;
                out.phase2TargetSessionAudioRelease = out.phase2TargetSessionAudioRelease || targetSessionAudioRelease;
                out.phase2ThisWindowTargetSessionAudioRelease =
                    out.phase2ThisWindowTargetSessionAudioRelease || targetSessionAudioRelease;
                out.phase2TargetSecurityStateFromPtt = out.phase2TargetSecurityStateFromPtt || burst.securityStateFromPtt;
                if (burst.essObservedThisBurst && burst.essKnown) {
                    out.phase2TargetEssKnown = true;
                    out.phase2TargetEssEncrypted =
                        out.phase2TargetEssEncrypted || burst.essEncrypted;
                } else if (burst.essKnown && !burst.essEncrypted) {
                    // Sticky session clear may keep target ESS known for continuity,
                    // but never promote encrypted from non-observed paint (DEC-0059).
                    out.phase2TargetEssKnown = true;
                }
                if (trafficTalkgroupBelongs &&
                    burst.trafficSecurityObservedThisBurst &&
                    burst.trafficSecurityKnown) {
                    out.phase2TargetEssKnown = true;
                    out.phase2TargetEssEncrypted =
                        out.phase2TargetEssEncrypted || burst.trafficEncrypted;
                } else if (trafficTalkgroupBelongs &&
                           burst.trafficSecurityKnown &&
                           !burst.trafficEncrypted) {
                    out.phase2TargetEssKnown = true;
                }
                // Capture 20260811_080304: sticky essKnown/trafficSecurityKnown on
                // every Voice2/4 made ThisWindowTargetEssClear a lie and opened
                // dual-slot MAC-dead feed (477 dual+mac0). Only this-burst ESS /
                // traffic-SO observation may set the this-window clear flag.
                if (burst.essObservedThisBurst && burst.essKnown && !burst.essEncrypted) {
                    out.phase2ThisWindowTargetEssClear = true;
                }
                if (burst.essObservedThisBurst && burst.essKnown && burst.essEncrypted) {
                    out.phase2ThisWindowTargetEssEncrypted = true;
                }
                if (trafficTalkgroupBelongs &&
                    burst.trafficSecurityObservedThisBurst &&
                    burst.trafficSecurityKnown &&
                    !burst.trafficEncrypted) {
                    out.phase2ThisWindowTargetEssClear = true;
                }
                if (trafficTalkgroupBelongs &&
                    burst.trafficSecurityObservedThisBurst &&
                    burst.trafficSecurityKnown &&
                    burst.trafficEncrypted) {
                    out.phase2ThisWindowTargetEssEncrypted = true;
                }
                if (!burst.voiceCodewords.empty()) {
                    selectedSlotHasVoiceCodewords = true;
                    out.phase2TargetVoiceCodewords += burst.voiceCodewords.size();
                }
                if (burst.xorMaskApplied) {
                    out.phase2TargetMaskedBursts += 1;  // count bursts with mask on target
                }
            } else if (!burst.voiceCodewords.empty()) {
                oppositeSlotHasVoiceCodewords = true;
                out.phase2OppositeVoiceCodewords += burst.voiceCodewords.size();
            }
        }
        // Override aggregate ESS fields for Phase 2 diagnostics/gating so the UI
        // and security gate describe the followed TDMA slot, not whichever slot in
        // the RF superframe happened to produce ESS first.
        out.phase2EssKnown = out.phase2TargetEssKnown;
        out.phase2EssEncrypted = out.phase2TargetEssKnown && out.phase2TargetEssEncrypted;
    }

    // Keep the grant slot authoritative.  Earlier builds auto-swapped target
    // and opposite-slot VCW counts when the other slot had voice; that can open
    // the wrong call or produce scrambled bursts.  Slot correction belongs in
    // explicit grant/identifier decoding or a user-visible slot-probe decision,
    // not in the audio release path.

    const bool currentSuperframeMaskLock =
        out.phase2SuperframeBursts >= 3 &&
        out.phase2MaskedBursts >= 3;
    const bool currentTargetStructuredMac =
        out.phase2TargetMacCrcValid &&
        (out.phase2TargetEssKnown ||
         out.phase2TargetSessionAudioRelease ||
         currentSuperframeMaskLock ||
         out.phase2TargetVoiceCodewords > 0);
    const bool currentAnyStructuredMac =
        out.phase2MacCrcValid > 0 &&
        (currentSuperframeMaskLock ||
         out.phase2VoiceCodewords > 0 ||
         live.stats.phase2IschDecoded > 0 ||
         out.phase2TargetEssKnown);
    if (audioKey.valid() &&
        (currentTargetStructuredMac ||
         currentAnyStructuredMac ||
         out.phase2TargetEssKnown ||
         out.phase2TargetSessionAudioRelease ||
         currentSuperframeMaskLock)) {
        p25RefreshPhase2RecentSecurityEvidence(
            rx,
            audioKey,
            nowMs,
            out.phase2TargetMacCrcValid,
            out.phase2MacCrcValid > 0,
            out.phase2TargetEssKnown,
            out.phase2TargetEssEncrypted,
            out.phase2TargetSessionAudioRelease,
            out.phase2TargetSecurityStateFromPtt,
            currentSuperframeMaskLock);
    }

    const bool recentSecurityEvidence =
        p25Phase2RecentSecurityEvidenceUsable(rx, audioKey, nowMs);
    const bool recentMacEvidenceForCall =
        recentSecurityEvidence &&
        (rx.p25Phase2RecentTargetMacCrcValid ||
         rx.p25Phase2RecentAnyMacCrcValid);
    const bool recentSuperframeMaskEvidenceForCall =
        recentSecurityEvidence &&
        rx.p25Phase2RecentSuperframeMaskLock;
    if (recentSecurityEvidence) {
        if (rx.p25Phase2RecentTargetMacCrcValid) {
            out.phase2TargetMacCrcValid = true;
        }
        if (rx.p25Phase2RecentTargetSessionAudioRelease) {
            out.phase2TargetSessionAudioRelease = true;
            out.phase2TargetSecurityStateFromPtt =
                out.phase2TargetSecurityStateFromPtt ||
                rx.p25Phase2RecentTargetSecurityStateFromPtt;
        }
        if (rx.p25Phase2RecentTargetEssKnown) {
            out.phase2TargetEssKnown = true;
            // This-window target clear wins over sticky recent encrypted.
            if (out.phase2ThisWindowTargetEssClear) {
                out.phase2TargetEssEncrypted = false;
            } else if (!out.phase2ThisWindowTargetEssEncrypted) {
                out.phase2TargetEssEncrypted =
                    out.phase2TargetEssEncrypted || rx.p25Phase2RecentTargetEssEncrypted;
            }
            out.phase2EssKnown = true;
            out.phase2EssEncrypted = out.phase2TargetEssEncrypted;
        }
    }

    bool sdrtrunkLateEntryVoiceRelease =
        p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out);
    out.phase2SdrtrunkLateEntryVoiceRelease =
        out.phase2SdrtrunkLateEntryVoiceRelease || sdrtrunkLateEntryVoiceRelease;

    auto canDrainPendingRawVoiceThisWindow = [&]() noexcept {
        if (out.skippedEncrypted ||
            out.phase2TargetEssEncrypted ||
            out.phase2WrongSlot) {
            return false;
        }
        const bool callAlreadyOpenedSpeaker =
            rx.p25Phase2CallHadSpeakerAudio ||
            rx.p25SessionState.sustain.hadSuccessfulEmit;
        if (callAlreadyOpenedSpeaker) {
            // SDRTrunk queues Phase-2 voice timeslots only until the current
            // call's clear/encrypted state is established. Once selected-slot
            // audio has reached the speaker, any remaining pending AMBE is old
            // late-entry/bootstrap material and must not be dripped into later
            // live windows, where it sounds like doubled or out-of-order speech.
            return false;
        }
        // DEC-0059: never drain pending into an opposite-only window (145139
        // first emit targetVcw=0 pendingRel=8 after companion dwell).
        if (out.phase2TargetVoiceCodewords == 0 &&
            out.phase2OppositeVoiceCodewords > 0) {
            return false;
        }
        const bool noLiveVoiceInWindow =
            out.phase2TargetVoiceCodewords == 0 &&
            out.phase2OppositeVoiceCodewords == 0;
        if (noLiveVoiceInWindow &&
            p25Phase2PendingAudioMatches(rx, audioKey) &&
            p25Phase2PendingAmbeFrameCount(rx, audioKey) > 0 &&
            !callAlreadyOpenedSpeaker) {
            // SDRTrunk releases queued voice timeslots when clear traffic
            // security is first established. After the speaker has already
            // opened, no-live pending drains are stale backlog and sound like
            // delayed/doubled speech between real selected-slot bursts.
            return establishedClearCall ||
                recentClearSecurityForCall ||
                p25Phase2TargetHardClearEvidence(out) ||
                out.phase2TargetMacCrcValid ||
                out.phase2MacCrcValid > 0;
        }
        // Capture 20260811_021036: pendingRel+live on dual-slot windows
        // (L27792 fed=16 pendRel=8 opp=8; L35906 fed=18 pendRel=8 ctx=4/0)
        // concatenates time-displaced AMBE into one PCM burst -> short
        // dual-voice / echo. Allow merge only after the selected slot has
        // current-window MAC/ESS proof; aggregate MAC or a superframe-only
        // selected burst can still be a wrong-epoch dual-slot window.
        if (out.phase2OppositeVoiceCodewords > 0 &&
            out.phase2TargetVoiceCodewords > 0 &&
            !out.phase2ThisWindowTargetMacCrcValid &&
            !out.phase2ThisWindowTargetEssClear) {
            return false;
        }
        // After the call has already spoken, drip late-entry stash only on
        // empty-target ticks unless the current selected burst is feed-trusted.
        if ((rx.p25Phase2CallHadSpeakerAudio ||
             rx.p25SessionState.sustain.hadSuccessfulEmit) &&
            out.phase2TargetVoiceCodewords > 0 &&
            !currentWindowHasFeedTrustedTargetBurst) {
            return false;
        }
        // Capture 20260811_080304 / 021036: dual-slot pending drain must be
        // fail-closed. Session-release / sticky MAC must not escape the
        // DualSlotUntrusted gate into concatenated pending+live PCM.
        if (p25Phase2DualSlotPendingDrainUnsafeWindow(out)) return false;
        if (out.phase2OppositeVoiceCodewords > 0) {
            (void)currentWindowHasFeedTrustedTargetBurst;
            return out.phase2ThisWindowTargetMacCrcValid ||
                out.phase2ThisWindowTargetEssClear;
        }
        return currentWindowHasFeedTrustedTargetBurst ||
            out.phase2MacCrcValid > 0;
    };

    auto discardStalePendingWhenLivePreferred = [&]() {
        if (!audioKey.valid()) return;
        const bool callAlreadyOpenedSpeaker =
            rx.p25Phase2CallHadSpeakerAudio ||
            rx.p25SessionState.sustain.hadSuccessfulEmit;
        if (!callAlreadyOpenedSpeaker) {
            return;
        }
        if (!p25Phase2PendingAudioMatches(rx, audioKey) ||
            p25Phase2PendingAmbeFrameCount(rx, audioKey) == 0) {
            return;
        }
        if (out.phase2TargetVoiceCodewords == 0) {
            p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::LiveStreamPreferred);
            return;
        }
        const auto& pendingQueue = rx.p25SessionState.pendingAudio;
        bool hasForwardPending = false;
        if (out.phase2LastFedAbsDibit != 0) {
            for (const auto& frame : pendingQueue.ambeFrames) {
                if (!frame.haveAbsoluteDibits ||
                    frame.codewordAbsDibit + 12u > out.phase2LastFedAbsDibit) {
                    hasForwardPending = true;
                    break;
                }
            }
        } else {
            hasForwardPending = true;
        }
        if (hasForwardPending) {
            return;
        }
        p25ClearPhase2PendingAudioOnly(rx, P25PendingClearReason::LiveStreamPreferred);
    };

    auto drainPendingRawVoice = [&]() {
        if (!audioKey.valid() || drainedPendingRawVoice) return;
        if (!canDrainPendingRawVoiceThisWindow()) {
            discardStalePendingWhenLivePreferred();
            return;
        }
        // Stream queued late-entry AMBE: one short cadence-aligned batch per
        // decode window. Dumping the whole stash (previously up to ~1s) floods
        // the speaker ring then starves into blocky islands. Leftover frames
        // stay armed for the next window.
        auto pendingFrames = p25TakePhase2PendingAudio(
            rx, audioKey, kP25Phase2PendingDrainMaxFramesPerTick);
        drainedPendingRawVoice = true;
        if (pendingFrames.empty()) return;
        sawVoice = true;
        out.phase2PendingAmbeFramesReleased += pendingFrames.size();
        attemptedNewVoice = true;
        for (const auto& pending : pendingFrames) {
            P25Phase2AmbeValidationFrame frame;
            frame.voiceIndex = pending.voiceIndex;
            frame.grantSlotKnown = pending.grantSlotKnown;
            frame.grantSlot = pending.grantSlot;
            frame.haveAbsoluteDibits = pending.haveAbsoluteDibits;
            frame.codewordAbsDibit = pending.codewordAbsDibit;
            frame.codewordEndAbsDibit = pending.codewordEndAbsDibit;
            const P25Phase2AmbeInputQuality pendingInputQuality =
                p25Phase2AmbeInputQualityFromPending(pending);
            p25Phase2ApplyAmbeInputQualityToValidationFrame(pendingInputQuality, frame);

            if (!pending.grantSlotKnown) {
                ++out.phase2RejectedVoiceCodewords;
                out.phase2MetadataMissing = true;
                out.phase2AudioLockMissing = true;
                ambeFrames.push_back(frame);
                continue;
            }
            if (rx.p25VoiceTdmaSlotKnown &&
                static_cast<uint8_t>(pending.grantSlot & 0x01u) !=
                    static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u)) {
                ++out.phase2RejectedVoiceCodewords;
                ++out.phase2WrongSlotVoiceCodewords;
                if (out.phase2TargetVoiceCodewords == 0 &&
                    !rx.p25Phase2GrantedSlotImmutable) {
                    out.phase2WrongSlot = true;
                }
                ambeFrames.push_back(frame);
                continue;
            }

            const Phase2VoiceFrameKey frameKey = p25Phase2VoiceFrameKeyFromPending(pending);
            const bool protocolKeyed = p25Phase2VoiceFrameKeyHasProtocolIdentity(frameKey);
            (void)p25Phase2SyncAmbeEmitDedupeCallContext(rx);
            // SDRTrunk never re-plays a stream position.  Overlapping IQ
            // windows must still drop already-emitted abs dibits, including
            // protocol-keyed Voice2/Voice4 frames — those keys wobble when
            // block-channelize resets the framer, which is the double-up path.
            if (!p25Phase2ShouldEmitAmbeFrame(rx,
                                             pending.codewordAbsDibit,
                                             pending.codewordEndAbsDibit,
                                             pending.haveAbsoluteDibits,
                                             false)) {
                skippedDuplicateVoice = true;
                ++out.phase2DuplicateSuppressedVoiceCodewords;
                ++out.phase2AbsoluteDuplicateSuppressedVoiceCodewords;
                frame.duplicateSuppressed = true;
                frame.duplicateSuppressedByAbsolute = true;
                ambeFrames.push_back(frame);
                continue;
            }
            if (p25Phase2LatticeKeyAlreadyEmitted(rx, frameKey, pending.codewordAbsDibit,
                                                 pending.haveAbsoluteDibits, nowMs)) {
                skippedDuplicateVoice = true;
                ++out.phase2DuplicateSuppressedVoiceCodewords;
                frame.duplicateSuppressed = true;
                ambeFrames.push_back(frame);
                continue;
            }

            if (!protocolKeyed) {
                // SDRTrunk queues whole voice timeslots until PTT/ESS proves
                // clear, then drains them in arrival order.  Our pending AMBE
                // queue can contain late-entry frames that lack full protocol
                // stream/session keys; sending those through the strict live
                // sequencer drops them as seqDrop and starves mbelib even though
                // the selected-slot VCWs are present.
                p25Phase2FillFeedGapWithPlc(rx,
                                            out,
                                            outputRateHz,
                                            pending.codewordAbsDibit,
                                            pending.haveAbsoluteDibits);
                p25RecordPhase2AmbeFeedCadence(out,
                                               pending.codewordAbsDibit,
                                               pending.haveAbsoluteDibits);

                frame.lockedVariantBefore = 0;
                const bool ok = p25DecodePhase2AmbeFrameToAudio(
                    rx, pending.ambe96, outputRateHz, out, frame, pendingInputQuality);
                if (ok || frame.timelineEmitted) {
                    p25Phase2RememberEmittedAmbeFrame(rx,
                                                      pending.codewordAbsDibit,
                                                      pending.codewordEndAbsDibit,
                                                      pending.haveAbsoluteDibits,
                                                      &frameKey,
                                                      nowMs);
                }
                if (ok) {
                    acceptedVoice = true;
                    acceptedReleaseVoice = true;
                }
                frame.lockedVariantAfter = 0;
                ambeFrames.push_back(frame);
                continue;
            }

            P25Phase2SequencerSpeechInput seqInput;
            seqInput.key = frameKey;
            seqInput.ambe96 = pending.ambe96;
            seqInput.haveAmbe = true;
            seqInput.grantSlotKnown = pending.grantSlotKnown;
            seqInput.grantSlot = pending.grantSlot;
            seqInput.haveAbsoluteDibits = pending.haveAbsoluteDibits;
            seqInput.codewordAbsDibit = pending.codewordAbsDibit;
            seqInput.codewordEndAbsDibit = pending.codewordEndAbsDibit;
            p25Phase2ApplyAmbeInputQualityToSpeechInput(pendingInputQuality, seqInput);
            const auto readySpeech =
                p25Phase2SequencerProcessSpeechFrame(rx, seqInput, &out, outputRateHz);
            if (readySpeech.empty()) {
                skippedDuplicateVoice = true;
                ++out.phase2DuplicateSuppressedVoiceCodewords;
                ++out.phase2SequencerSuppressedVoiceCodewords;
                frame.duplicateSuppressed = true;
                frame.duplicateSuppressedBySequencer = true;
                ambeFrames.push_back(frame);
                continue;
            }
            std::vector<uint64_t> readyAbsStarts;
            for (const auto& speechItem : readySpeech) {
                P25Phase2AmbeValidationFrame speechFrame = frame;
                speechFrame.haveAbsoluteDibits = speechItem.haveAbsoluteDibits;
                speechFrame.codewordAbsDibit = speechItem.codewordAbsDibit;
                speechFrame.codewordEndAbsDibit = speechItem.codewordEndAbsDibit;
                const P25Phase2AmbeInputQuality speechInputQuality =
                    p25Phase2AmbeInputQualityFromSpeechInput(speechItem);
                p25Phase2ApplyAmbeInputQualityToValidationFrame(speechInputQuality, speechFrame);
                bool duplicateInReadyBatch = false;
                if (speechItem.haveAbsoluteDibits) {
                    duplicateInReadyBatch = std::any_of(
                        readyAbsStarts.begin(),
                        readyAbsStarts.end(),
                        [&](uint64_t prior) {
                            constexpr uint64_t kReadyBatchDuplicateToleranceDibits = 12u;
                            const uint64_t delta = speechItem.codewordAbsDibit > prior
                                ? speechItem.codewordAbsDibit - prior
                                : prior - speechItem.codewordAbsDibit;
                            return delta <= kReadyBatchDuplicateToleranceDibits;
                        });
                }
                if (duplicateInReadyBatch ||
                    !p25Phase2ShouldEmitAmbeFrame(rx,
                                                 speechItem.codewordAbsDibit,
                                                 speechItem.codewordEndAbsDibit,
                                                 speechItem.haveAbsoluteDibits,
                                                 false) ||
                    p25Phase2LatticeKeyAlreadyEmitted(rx, speechItem.key, speechItem.codewordAbsDibit,
                                                     speechItem.haveAbsoluteDibits, nowMs)) {
                    skippedDuplicateVoice = true;
                    ++out.phase2DuplicateSuppressedVoiceCodewords;
                    ++out.phase2AbsoluteDuplicateSuppressedVoiceCodewords;
                    speechFrame.duplicateSuppressed = true;
                    speechFrame.duplicateSuppressedByAbsolute = true;
                    ambeFrames.push_back(speechFrame);
                    continue;
                }
                if (speechItem.haveAbsoluteDibits) {
                    readyAbsStarts.push_back(speechItem.codewordAbsDibit);
                }

                p25Phase2FillFeedGapWithPlc(rx,
                                            out,
                                            outputRateHz,
                                            speechItem.codewordAbsDibit,
                                            speechItem.haveAbsoluteDibits);
                p25RecordPhase2AmbeFeedCadence(out,
                                               speechItem.codewordAbsDibit,
                                               speechItem.haveAbsoluteDibits);

                speechFrame.lockedVariantBefore = 0;
                const bool ok = p25DecodePhase2AmbeFrameToAudio(
                    rx, speechItem.ambe96, outputRateHz, out, speechFrame, speechInputQuality);
                if (ok || speechFrame.timelineEmitted) {
                    p25Phase2RememberEmittedAmbeFrame(rx,
                                                      speechItem.codewordAbsDibit,
                                                      speechItem.codewordEndAbsDibit,
                                                      speechItem.haveAbsoluteDibits,
                                                      &speechItem.key,
                                                      nowMs);
                }
                if (ok || speechFrame.timelineEmitted) {
                    p25Phase2RecordEmittedSpeechOrdinal(out, speechItem);
                }
                if (ok) {
                    acceptedVoice = true;
                    acceptedReleaseVoice = true;
                }
                speechFrame.lockedVariantAfter = 0;
                ambeFrames.push_back(speechFrame);
            }
        }
    };

    auto releasePendingRawVoiceFromEss = [&]() {
        if (!audioKey.valid() || acceptedReleaseVoice || drainedPendingRawVoice) return;
        if (!canDrainPendingRawVoiceThisWindow()) return;
        // Match sdrtrunk's P25P2AudioModule ordering: audio timeslots are queued
        // until current-call security is known. Push-To-Talk starts/resets the
        // call and clears any stale queued voice; a later valid clear ESS drains
        // queued voice even if the current decoder tick has no fresh VCWs.
        if (out.phase2TargetSessionAudioRelease && out.phase2TargetSecurityStateFromPtt) {
            if (!p25Phase2PendingAudioMatches(rx, audioKey) ||
                rx.p25SessionState.pendingAudio.ambeFrames.empty()) {
                p25Phase2HandlePttStartForPendingQueue(rx, audioKey);
            } else {
                drainPendingRawVoice();
            }
            drainedPendingRawVoice = true;
            return;
        }
        const bool targetEssClear =
            out.phase2TargetEssKnown &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2TargetSecurityStateFromPtt;
        if (targetEssClear) {
            drainPendingRawVoice();
        }
    };

    auto releasePendingRawVoiceFromTrustedTrafficState = [&]() {
        if (!audioKey.valid() || acceptedReleaseVoice || drainedPendingRawVoice) return;
        if (!canDrainPendingRawVoiceThisWindow()) return;
        const bool trustedClear = sdrtrunkLateEntryVoiceRelease &&
            audioKey.valid() &&
            p25Phase2PendingAudioMatches(rx, audioKey);
        if (trustedClear) {
            drainPendingRawVoice();
        }
    };

    auto releasePendingRawVoiceFromExplicitClearTrafficProof = [&]() {
        if (!audioKey.valid() || acceptedReleaseVoice || drainedPendingRawVoice) return;
        if (!canDrainPendingRawVoiceThisWindow()) return;
        // Capture 20260808_012422: clear grant + target MAC CRC (or prior emit /
        // clear latch) must drain the raw AMBE queue — waiting only for ESS left
        // pending frames stranded when the next hop lost CQPSK lock.
        const bool explicitClearTrafficProof =
            p25Phase2ExplicitClearGrantVoiceReleaseEvidence(rx, out);
        const bool sameCallEstablishedClearDrain =
            explicitClearGrantForCall &&
            !out.phase2WrongSlot &&
            !out.phase2TargetEssEncrypted &&
            establishedClearCall &&
            (rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear ||
             rx.p25SessionState.sustain.hadSuccessfulEmit ||
             p25Phase2SessionSpeakerSustainActive(rx));
        if (!explicitClearTrafficProof && !sameCallEstablishedClearDrain) return;

        drainPendingRawVoice();
        // The control-channel grant selects the traffic slot; target-slot PTT/ESS
        // or retained target traffic proof releases the queued voice frames.
        if (acceptedReleaseVoice && p25AudioSamplesLookSafe(out.audio)) {
            out.phase2ExplicitClearGrantVoiceRelease = true;
            out.phase2SecurityTrustedClear = true;
            out.phase2SecurityUnknown = false;
            out.phase2SecurityGateAction = "explicit-clear-grant-traffic-clear-release";
            out.phase2CurrentProbePcmUsable = true;
            out.phase2UnknownProbeQualityOk = true;
            out.phase2UnknownProbeBlockReason = "explicit-clear-grant-traffic-clear-release";
            p25RefreshPhase2RecentSecurityEvidence(
                rx,
                audioKey,
                nowMs,
                out.phase2TargetMacCrcValid,
                out.phase2MacCrcValid > 0,
                out.phase2TargetEssKnown,
                out.phase2TargetEssEncrypted,
                out.phase2TargetSessionAudioRelease,
                out.phase2TargetSecurityStateFromPtt,
                out.phase2SuperframeBursts > 0 && out.phase2MaskedBursts > 0);
        }
    };

    auto keepUnknownGrantProbeDiagnosticOnly = [&]() {
        if (!audioKey.valid() || acceptedReleaseVoice || drainedPendingRawVoice) return;
        if (!out.phase2FieldAudioProbeAllowed) return;
        // Keep queued voice untouched. Diagnostic AMBE probes use a throwaway
        // decoder and are reported in validation logs; only PTT/ESS can release.
        out.phase2UnknownProbeBlockReason = "late-entry-audio-probe-diagnostic-only";
    };

    // Ensure we feed mbelib in strict chronological order even if live bursts were collected
    // from mixed locked + sticky paths. Out-of-order AMBE frames corrupt the predictor
    // and produce blocky/not-joined audio. Persistent-framer bursts can all carry
    // dibitOffset=0, so prefer the monotonic stream coordinate when it is present.
    std::vector<P25Phase2Burst> orderedBurstsForFeed = live.phase2Bursts;
    std::stable_sort(orderedBurstsForFeed.begin(), orderedBurstsForFeed.end(), [](const P25Phase2Burst& a, const P25Phase2Burst& b) {
        if (a.streamBurstStartDibitKnown && b.streamBurstStartDibitKnown &&
            a.streamBurstStartDibit != b.streamBurstStartDibit) {
            return a.streamBurstStartDibit < b.streamBurstStartDibit;
        }
        if (a.streamBurstStartDibitKnown != b.streamBurstStartDibitKnown) {
            return a.streamBurstStartDibitKnown;
        }
        return a.dibitOffset < b.dibitOffset;
    });

    // Compute "what we need" for the selected timeslot only.  A Phase-2 RF
    // carrier may carry two calls at once; counting the companion slot here made
    // good selected-slot output look half-rate and hid real cadence failures.
    for (const auto& b : orderedBurstsForFeed) {
        if (b.voiceCodewords.empty()) continue;
        if (b.kind != P25Phase2BurstKind::Voice4 &&
            b.kind != P25Phase2BurstKind::Voice2) {
            continue;
        }
        if (rx.p25VoiceTdmaSlotKnown) {
            if (!b.grantSlotKnown) continue;
            const uint8_t burstSlot = static_cast<uint8_t>(b.grantSlot & 0x01u);
            if (burstSlot != static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u)) {
                continue;
            }
            if (p25Phase2TrafficTalkgroupKnownMismatch(rx, b)) {
                continue;
            }
            if (selectedSlotKnownOtherTalkgroupInWindow &&
                (!b.trafficTalkgroupKnown ||
                 b.trafficTalkgroupId != rx.p25VoiceTalkgroupId)) {
                continue;
            }
        }
        for (const auto& cw : b.voiceCodewords) {
            const bool cwAbsKnown = codewordAbsoluteDibitKnown(cw);
            const uint64_t cwAbsDibit = codewordAbsoluteDibit(cw);
            const uint64_t cwEndAbsDibit = cwAbsKnown ? cwAbsDibit + 36u : 0u;
            if (codewordIsContextOnly(cwAbsKnown, cwEndAbsDibit)) {
                continue;
            }
            ++out.phase2ExpectedVoiceCodewords;
        }
    }

    for (const auto& burst : orderedBurstsForFeed) {
        // Apply MAC transitions in the same capture order as speech. A prepass
        // over all MAC messages applies a later END to earlier voice in a window.
        if (rx.p25VoiceTdmaSlotKnown && burst.grantSlotKnown) {
            uint8_t slot = static_cast<uint8_t>(burst.grantSlot & 0x01u);
            if (phase2InvertSlotLabelsForWindow) slot ^= 0x01u;
            const bool positionKnown = burst.streamBurstStartDibitKnown || haveAbsoluteDibits;
            const uint64_t position = burst.streamBurstStartDibitKnown
                ? burst.streamBurstStartDibit : windowStartAbsDibit + burst.dibitOffset;
            p25Phase2ObserveTargetTalkspurtMac(rx, burst,
                slot == static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u),
                nowMs, positionKnown, position);
        }
        if (burst.voiceCodewords.empty()) continue;
        // Match sdrtrunk: only hard Voice2/Voice4 timeslots feed AMBE audio.
        // UnknownTimeslot-equivalent bursts remain diagnostics and never become
        // speaker audio; otherwise signaling bits can sound like scrambled voice.
        const bool hardVoiceKind =
            burst.kind == P25Phase2BurstKind::Voice4 ||
            burst.kind == P25Phase2BurstKind::Voice2;
        if (!hardVoiceKind) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            continue;
        }
        sawVoice = true;
        const bool grantClearTrusted = explicitClearGrantForCall;
        const bool grantUnknownProbe =
            rx.p25VoicePhase2 &&
            rx.p25VoiceMaskParamsKnown &&
            !rx.p25VoiceEncrypted &&
            !recentClearSecurityForCall;
        const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;
        // Unknown Phase-2 grants may be followed and decoded for diagnostics/short
        // pending queues, but they are not clear-audio proof.  sdrtrunk queues
        // voice timeslots until PTT/ESS establishes clear/encrypted state; AMBE
        // plausibility alone must never clear release gates, and raw scrambled AMBE must
        // stay fail-closed unless the bounded target-slot recovery
        // gate later proves the current window produced usable PCM.
        // Preferred release is target-slot PTT/ESS, exactly like sdrtrunk.
        // Field recovery fallback: once the current traffic call has established
        // clear state, or target-slot late-entry MAC/voice evidence matches
        // sdrtrunk's traffic-channel state, allow AMBE PCM to reach the speaker.
        // This fallback never applies to explicit encrypted grants/ESS.
        const qint64 grantAgeMs = nowMs - rx.p25VoiceGrantEpochMs;
        const bool releaseHasRequiredLock = burst.superframeLock;
        const bool grantMayReleaseVoice =
            sdrtrunkLateEntryVoiceRelease &&
            rx.p25VoiceMaskParamsKnown &&
            burst.xorMaskApplied &&
            releaseHasRequiredLock &&
            grantAgeMs >= 0 &&
            !out.phase2TargetEssEncrypted;
        if (!rx.p25VoiceTdmaSlotKnown) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2MetadataMissing = true;
            continue;
        }
        // Hard slot ownership (sdrtrunk one AudioModule per timeslot).  Unlabelled
        // bursts previously fell through grantMayProbeVoice and were treated as
        // slot 0 — dual-call mix with oppVcw=0 in logs.
        if (!burst.grantSlotKnown) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2AudioLockMissing = true;
            out.phase2MetadataMissing = true;
            continue;
        }
        const bool forceEstablishedFeed = establishedClearCall &&
            p25Phase2EstablishedClearNoiseFeedAllowed(rx, out, burst, recentMacEvidenceForCall);
        // Capture 20260811_080304: sessionAudioRelease is security continuity,
        // not XOR/mask epoch. Align soft epoch with hardEpochOnBurst so sticky
        // PTT/session cannot walk Voice2/4 into mbelib before mask/SF/MAC proof.
        // DEC-0055.2: remove bare establishedClear+xor+grantSlot epoch. That soft
        // arm fed wrong sticky phase as continuous garble. Continuity after clear
        // uses this-burst SF/mask/MAC above, or forceEstablishedFeed (mac /
        // maskPhaseLock / tail+mask only). Prefer a short hole over garbage PCM.
        const bool epochTrusted =
            burst.superframeLock ||
            burst.maskPhaseLock ||
            burst.macCrcValid ||
            burst.macCrcLock ||
            (burst.xorMaskPhaseKnown && burst.superframeLock);
        if (!epochTrusted && !grantMayProbeVoice && !forceEstablishedFeed) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2AudioLockMissing = true;
            out.phase2MetadataMissing = true;
            continue;
        }
        if (!epochTrusted && grantMayProbeVoice) {
            out.phase2AudioLockMissing = true;
            out.phase2MetadataMissing = true;
        }
        const uint8_t followedGrantSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
        // Invert remains forced false; keep the xor for clarity only.
        const uint8_t effectiveBurstSlot =
            static_cast<uint8_t>((burst.grantSlot ^ (phase2InvertSlotLabelsForWindow ? 0x01u : 0x00u)) & 0x01u);
        const bool burstEncryptedForFollowedCall =
            p25Phase2BurstEncryptedForFollowedCall(rx, burst);
        if (effectiveBurstSlot != followedGrantSlot) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2WrongSlotVoiceCodewords += burst.voiceCodewords.size();
            // A Phase-2 RF carrier carries both TDMA slots (often two different TGs).
            // sdrtrunk binds each call to one TIMESLOT audio module and never feeds
            // the other into the selected speaker.  Observe/decode the companion
            // into the opposite AMBE module (stats + pending only).
            p25Phase2ObserveOppositeSlotAmbe(
                rx, burst, effectiveBurstSlot, targetFreqHz, out);
            // DEC-0057: CC grant slot immutable → companion-only dwell is normal,
            // not "wrong TDMA slot". Capture 135857 TG30003 slot=1 clear: 75
            // wrong-TDMA status lines with p2vcw>0 decoded=0 while target emits
            // were CLEAR — opposite-slot talker during our silence.
            if (!selectedSlotHasVoiceCodewords &&
                oppositeSlotHasVoiceCodewords &&
                !rx.p25Phase2GrantedSlotImmutable) {
                out.phase2WrongSlot = true;
            }
            continue;
        }
        if (p25Phase2TrafficTalkgroupKnownMismatch(rx, burst)) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            if (p25Phase2TrafficTalkgroupAuthoritativeThisBurst(burst)) {
                out.phase2WrongSlotVoiceCodewords += burst.voiceCodewords.size();
                out.phase2TrafficTalkgroupMismatchVoiceCodewords += burst.voiceCodewords.size();
            } else {
                out.phase2TrafficTalkgroupStaleMismatchVoiceCodewords += burst.voiceCodewords.size();
            }
            continue;
        }
        if (selectedSlotKnownOtherTalkgroupInWindow &&
            (!burst.trafficTalkgroupKnown ||
             burst.trafficTalkgroupId != rx.p25VoiceTalkgroupId)) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2TrafficTalkgroupStaleMismatchVoiceCodewords += burst.voiceCodewords.size();
            continue;
        }
        if (p25Phase2TrafficTalkgroupMismatchBlocksSelectedSlot(rx, burst)) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2WrongSlotVoiceCodewords += burst.voiceCodewords.size();
            out.phase2TrafficTalkgroupMismatchVoiceCodewords += burst.voiceCodewords.size();
            continue;
        }
        // OP25 and sdrtrunk both descramble Phase-2 traffic before extracting
        // voice frames.  Clear call state can only preserve an already-established
        // speaker release; it cannot make a scrambled AMBE payload decodable.
        if (!burst.xorMaskApplied) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2MaskMissing = true;
            out.phase2AudioLockMissing = true;
            continue;
        }
        // Trusted descramble on the *known matching* grant slot (never unlabelled).
        // Established clear + xor applied + correct grant slot: do not re-require
        // per-burst SF/MAC after the call is already open (20260808_001448
        // clear-grant-vcw-not-fed / vcw-present-but-no-sf-mask-yet while
        // callClearTrusted=yes and dutySec only ~0.5).
        // sameCallClearSustainFeed is computed later; establishedClearCall is enough
        // here to keep selected-slot Voice2/4 flowing once the call is open.
        const bool establishedClearSelectedSlot =
            (establishedClearCall || forceEstablishedFeed) &&
            burst.xorMaskApplied &&
            burst.grantSlotKnown &&
            effectiveBurstSlot == followedGrantSlot &&
            !burstEncryptedForFollowedCall;
        // Capture 20260811_080304: do not treat sessionAudioRelease as mask-phase
        // trust — same dual+mac0 path as soft epoch. Keep established clear +
        // this-burst MAC/mask/SF only.
        const bool maskPhaseTrusted =
            burst.maskPhaseLock ||
            burst.macCrcValid ||
            burst.macCrcLock ||
            establishedClearSelectedSlot ||
            (burst.xorMaskPhaseKnown &&
             burst.grantSlotKnown &&
             (burst.superframeLock || burst.stickySuperframe));
        // Live feed only when the selected-slot burst is trusted. Probe path may
        // continue for raw AMBE queue, but not for unlabeled / wrong-epoch VCW.
        if (!maskPhaseTrusted && !forceEstablishedFeed) {
            if (grantMayProbeVoice && burst.grantSlotKnown) {
                out.phase2AudioLockMissing = true;
                out.phase2MetadataMissing = true;
            } else {
                out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
                out.phase2AudioLockMissing = true;
                out.phase2MetadataMissing = true;
                continue;
            }
        }
        if (!maskPhaseTrusted) {
            out.phase2MetadataMissing = true;
            out.phase2AudioLockMissing = true;
        }
        const bool burstSdrtrunkLateEntryVoiceRelease =
            rx.p25VoicePhase2 &&
            rx.p25VoiceMaskParamsKnown &&
            !rx.p25VoiceEncrypted &&
            out.phase2TargetEssKnown &&
            !out.phase2TargetEssEncrypted &&
            !burstEncryptedForFollowedCall &&
            burst.xorMaskApplied &&
            burst.superframeLock &&
            (burst.macCrcValid || burst.macCrcLock || out.phase2TargetMacCrcValid || recentMacEvidenceForCall) &&
            out.phase2SuperframeBursts >= 6 &&
            out.phase2MaskedBursts >= 6;
        if (burstSdrtrunkLateEntryVoiceRelease) {
            sdrtrunkLateEntryVoiceRelease = true;
            out.phase2SdrtrunkLateEntryVoiceRelease = true;
        }
        const bool targetVoiceForLateEntryProbe = out.phase2TargetVoiceCodewords > 0 ||
            (!rx.p25VoiceTdmaSlotKnown && out.phase2VoiceCodewords > 0 &&
             out.phase2WrongSlotVoiceCodewords == 0 && !out.phase2WrongSlot);
        const bool macEvidenceForLateEntryProbe =
            burst.macCrcValid ||
            burst.macCrcLock ||
            out.phase2TargetMacCrcValid ||
            out.phase2MacCrcValid > 0 ||
            recentMacEvidenceForCall;
        const bool strongLateEntryVoiceEvidence =
            p25Phase2StrongLateEntryTargetVoiceEvidence(rx, out);
        const bool bootstrappedMaskEvidence =
            p25Phase2BootstrappedMaskTargetVoiceEvidence(out);
        const bool metadataEvidenceForLateEntryProbe =
            macEvidenceForLateEntryProbe || strongLateEntryVoiceEvidence || bootstrappedMaskEvidence;
        const bool superframeMaskEvidenceForLateEntryProbe =
            (out.phase2SuperframeBursts >= 6 &&
             out.phase2MaskedBursts >= 6) ||
            (recentSuperframeMaskEvidenceForCall &&
             out.phase2MaskedBursts > 0) ||
            bootstrappedMaskEvidence;
        const bool lateEntryProbeGraceSatisfied =
            grantAgeMs >= kP25Phase2UnknownGrantAudioProbeGraceMs ||
            metadataEvidenceForLateEntryProbe;
        const bool lateEntryAudioProbeAllowed =
            rx.p25Phase2AllowLateEntryAudioProbe &&
            grantMayProbeVoice &&
            rx.p25VoiceMaskParamsKnown &&
            !rx.p25VoiceEncrypted &&
            !out.phase2TargetEssEncrypted &&
            !burstEncryptedForFollowedCall &&
            burst.xorMaskApplied &&
            targetVoiceForLateEntryProbe &&
            metadataEvidenceForLateEntryProbe &&
            superframeMaskEvidenceForLateEntryProbe &&
            lateEntryProbeGraceSatisfied &&
            (burst.grantSlotKnown || rx.p25VoiceTdmaSlotKnown) &&
            (burst.superframeLock ||
             burst.maskPhaseLock ||
             burst.macCrcLock ||
             (out.phase2SuperframeBursts >= 6 && out.phase2MaskedBursts >= 6) ||
             strongLateEntryVoiceEvidence ||
             bootstrappedMaskEvidence);
        if (lateEntryAudioProbeAllowed) {
            out.phase2FieldAudioProbeAllowed = true;
            out.phase2UnknownProbeBlockReason = strongLateEntryVoiceEvidence && !macEvidenceForLateEntryProbe
                ? "late-entry-strong-target-voice-probe"
                : "late-entry-audio-probe";
        }
        const bool lateEntryStrongTargetReleaseAllowed =
            lateEntryAudioProbeAllowed &&
            macEvidenceForLateEntryProbe &&
            strongLateEntryVoiceEvidence &&
            out.phase2TargetEssKnown &&
            !out.phase2TargetEssEncrypted;
        const bool freshTargetTrafficClearEvidence =
            p25Phase2TargetHardClearEvidence(out) ||
            p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out) ||
            p25Phase2ExplicitClearGrantTargetMacTrafficProof(rx, out);
        const bool currentBurstFeedTrustedRaw =
            p25Phase2CurrentSelectedBurstFeedTrusted(burst, burstEncryptedForFollowedCall);
        // Immediate AMBE-to-speaker feed requires security already proved clear
        // by target-slot ESS/PTT or prior same-call target traffic state.
        // Capture 20260808_012422: need latch/MAC/post-emit open for Voice2/4.
        // Capture 20260808_021134: latch/post-emit WITHOUT hard epoch + dual-slot
        // ess=unknown produced mostly garble - require mask/SF/MAC on the burst
        // and refuse dual-slot untrusted windows.
        // Capture 20260808_034136: sticky ess=clear must NOT green-light dual-slot
        // MAC-dead feed (wrong-epoch AMBE -> blocky unintelligible speech).
        // Session/PTT release is clear/security continuity, not mask epoch.
        // Capture 20260811_072556: do not use it to prove descramble phase; the
        // window gate below still blocks unaccounted companion-slot traffic.
        const bool hardEpochOnBurst =
            burst.maskPhaseLock ||
            burst.superframeLock ||
            burst.macCrcValid ||
            burst.macCrcLock ||
            (burst.xorMaskPhaseKnown && burst.superframeLock);
        const bool sameCallContinuationStructure =
            p25Phase2SameCallSelectedTimeslotContinuationSafe(rx, out, audioKey, nowMs, false);
        // SDRTrunk keeps one traffic audio module per timeslot and carries the
        // clear/session epoch across voice-only TDMA bursts.  Field
        // 20260825_102710 shows selected-slot VCWs (targetVcw>0) being dropped in
        // dual-slot MAC-dead windows after ESS/PTT had already proved this call
        // clear.  Continue only the already-selected slot, never unlabelled or
        // opposite-slot VCWs.
        const bool carriedSelectedSlotEpoch =
            !currentBurstFeedTrustedRaw &&
            sameCallContinuationStructure &&
            establishedClearCall &&
            burst.xorMaskApplied &&
            burst.grantSlotKnown &&
            effectiveBurstSlot == followedGrantSlot &&
            !burstEncryptedForFollowedCall &&
            (recentMacEvidenceForCall ||
             recentSuperframeMaskEvidenceForCall);
        const bool currentBurstFeedTrusted =
            currentBurstFeedTrustedRaw || carriedSelectedSlotEpoch;
        if (currentBurstFeedTrusted && targetVoiceForLateEntryProbe) {
            currentWindowHasFeedTrustedTargetBurst = true;
            out.phase2CurrentFeedTrustedTargetBurst = true;
        }
        const bool recentClearContinuationEvidence =
            recentClearSecurityForCall &&
            currentBurstFeedTrusted &&
            targetVoiceForLateEntryProbe &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2WrongSlot &&
            !p25Phase2DualSlotUntrustedGarbleWindow(out) &&
            (out.phase2OppositeVoiceCodewords == 0 ||
             out.phase2ThisWindowTargetMacCrcValid ||
             out.phase2ThisWindowTargetEssClear ||
             out.phase2SameCallSelectedTimeslotContinuation);
        const bool targetTrafficClearEvidence =
            freshTargetTrafficClearEvidence || recentClearContinuationEvidence;
        const bool sameCallClearSustainFeed =
            explicitClearGrantForCall &&
            rx.p25VoiceMaskParamsKnown &&
            rx.p25VoiceTdmaSlotKnown &&
            !rx.p25VoiceEncrypted &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2WrongSlot &&
            currentBurstFeedTrusted &&
            targetVoiceForLateEntryProbe &&
            (targetTrafficClearEvidence ||
             establishedClearCall ||
             p25Phase2AudioTailGraceActive(rx) ||
             p25Phase2SessionSpeakerSustainActive(rx));
        const bool explicitGrantTargetSlotSelected =
            rx.p25VoiceTdmaSlotKnown &&
            out.phase2TargetVoiceCodewords >= kP25Phase2ExplicitClearGrantProbeMinFrames &&
            out.phase2TargetMaskedBursts > 0 &&
            !out.phase2WrongSlot;
        // Dual-slot without this-window selected-slot proof is never an
        // explicit-grant hard release (034136 sticky-clear dual-slot blocky).
        // Align with DualSlotUntrustedGarbleWindow (080304 feed/speaker parity).
        const bool dualSlotUntrustedExplicitGrant =
            !(burst.macCrcValid || burst.macCrcLock) &&
            (p25Phase2DualSlotUntrustedGarbleWindow(out) ||
             p25Phase2PostEmitMixedMacDeadWindow(rx, out));
        const bool explicitClearGrantProbeAllowed =
            explicitClearGrantForCall &&
            rx.p25VoiceMaskParamsKnown &&
            rx.p25VoiceTdmaSlotKnown &&
            !rx.p25VoiceEncrypted &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2WrongSlot &&
            !dualSlotUntrustedExplicitGrant &&
            burst.xorMaskApplied &&
            !burstEncryptedForFollowedCall &&
            targetVoiceForLateEntryProbe &&
            out.phase2TargetMaskedBursts > 0 &&
            (burst.grantSlotKnown || out.phase2TargetVoiceCodewords > 0);
        // Explicit clear CC grants select the traffic timeslot and may run a
        // throwaway AMBE probe for diagnostics. They must not feed the
        // persistent vocoder until target PTT/ESS/session state proves the
        // traffic slot clear; this matches SDRTrunk's P25P2AudioModule ordering.
        const bool explicitClearGrantHardVoiceRelease =
            explicitClearGrantForCall &&
            rx.p25VoiceMaskParamsKnown &&
            rx.p25VoiceTdmaSlotKnown &&
            !rx.p25VoiceEncrypted &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2WrongSlot &&
            !dualSlotUntrustedExplicitGrant &&
            freshTargetTrafficClearEvidence &&
            currentBurstFeedTrusted &&
            targetVoiceForLateEntryProbe &&
            out.phase2TargetMaskedBursts > 0 &&
            (burst.grantSlotKnown || out.phase2TargetVoiceCodewords > 0);
        if (explicitClearGrantHardVoiceRelease) {
            out.phase2ExplicitClearGrantVoiceRelease = true;
        }
        // Once the traffic slot is known clear, correctly descrambled 2V/4V
        // frames go straight to AMBE. Until then, match sdrtrunk's
        // P25P2AudioModule: retain voice frames as raw queued AMBE and wait for
        // PTT/ESS or a trusted target-slot clear state; explicit encrypted state
        // always wins.
        const bool clearMetadataTrusted = burst.macCrcLock ||
            (burst.essKnown && !burstEncryptedForFollowedCall && burst.sessionAudioRelease) ||
            sdrtrunkLateEntryVoiceRelease ||
            lateEntryStrongTargetReleaseAllowed ||
            explicitClearGrantHardVoiceRelease ||
            sameCallClearSustainFeed ||
            p25Phase2EstablishedClearNoiseFeedAllowed(rx, out, burst, recentMacEvidenceForCall);
        if (!clearMetadataTrusted) {
            out.phase2AudioLockMissing = true;
            out.phase2MaskAppliedNoMacCrc = true;
            // Unknown-security voice is queued below as raw AMBE, not decoded.
            // The flags are diagnostic so the UI can distinguish "queued for
            // PTT/ESS" from "no voice frames reached us".
        }
        if (!burst.essKnown && !burst.sessionAudioRelease) {
            out.phase2EssMissing = true;
            out.phase2LateEntryWaiting = true;
            out.phase2MetadataMissing = true;
        }
        if (burstEncryptedForFollowedCall) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.skippedEncrypted = true;
            continue;
        }
        const bool voiceReleaseTrusted =
            burst.sessionAudioRelease ||
            grantMayReleaseVoice ||
            explicitClearGrantHardVoiceRelease ||
            sameCallClearSustainFeed;
        const bool effectiveVoiceReleaseTrusted =
            voiceReleaseTrusted ||
            burstSdrtrunkLateEntryVoiceRelease ||
            lateEntryStrongTargetReleaseAllowed;
        if (!effectiveVoiceReleaseTrusted) {
            out.phase2AudioLockMissing = true;
        }
        const bool selectedSlotEpochForFeed = hardEpochOnBurst || carriedSelectedSlotEpoch;
        // Dual-slot feed fail-closed: match speaker DualSlotUntrustedGarbleWindow
        // (strong selected + companion accounted + honest this-window MAC/ESS).
        // Capture 20260811_080304: weaker dualSlotUntrustedNow treated sticky
        // ThisWindowTargetEssClear alone as proof → 477 dual+mac0 mbelib feeds
        // while speaker later muted → poison vocoder / blocky audio.
        const bool dualSlotSelectedContinuationForBurst =
            sameCallContinuationStructure &&
            currentBurstFeedTrusted &&
            effectiveBurstSlot == followedGrantSlot &&
            burst.xorMaskApplied &&
            selectedSlotEpochForFeed &&
            !burstEncryptedForFollowedCall;
        if (dualSlotSelectedContinuationForBurst) {
            out.phase2SameCallSelectedTimeslotContinuation = true;
        }
        // Continuation may still escape DualSlotUntrustedGarbleWindow (004206
        // ESS-clear mixed hops). DEC-0012 still blocks unproven post-emit
        // companion-louder windows, but the live feed must not starve the
        // selected SDRTrunk-style timeslot module when the current burst is
        // labelled, descrambled, same-call, and companion-accounted.
        const bool selectedPostEmitContinuationForBurst =
            dualSlotSelectedContinuationForBurst &&
            p25Phase2PostEmitSelectedSlotContinuationSafe(
                rx, out, audioKey, nowMs, /*requireFedAudio=*/false);
        const bool dualSlotUntrustedNow =
            !(burst.macCrcValid || burst.macCrcLock) &&
            ((p25Phase2DualSlotUntrustedGarbleWindow(out) &&
              !dualSlotSelectedContinuationForBurst) ||
             (p25Phase2PostEmitMixedMacDeadWindow(rx, out) &&
              !selectedPostEmitContinuationForBurst));
        // DEC-0058: latched clear + selected-dominant + known-clear ESS — do not
        // starve feed on MAC-dead dual-slot hops (142104 waiting-clear islands).
        const bool latchedSelectedDominantClearFeed =
            (rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear ||
             rx.p25SessionState.sustain.hadSuccessfulEmit ||
             rx.p25Phase2CallHadSpeakerAudio) &&
            out.phase2TargetEssKnown &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2WrongSlot &&
            out.phase2TargetVoiceCodewords >= 2 &&
            out.phase2TargetVoiceCodewords >= out.phase2OppositeVoiceCodewords &&
            p25Phase2CompanionSlotAccounted(out) &&
            p25Phase2StrongSelectedSlotStructure(out) &&
            effectiveBurstSlot == followedGrantSlot &&
            burst.xorMaskApplied &&
            !burstEncryptedForFollowedCall;
        const bool dualSlotUntrustedNowEffective =
            dualSlotUntrustedNow &&
            !latchedSelectedDominantClearFeed &&
            !selectedPostEmitContinuationForBurst;
        const bool clearLatchOpen =
            rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Clear &&
            !rx.p25VoiceEncrypted &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2WrongSlot &&
            selectedSlotEpochForFeed &&
            !dualSlotUntrustedNowEffective;
        const bool postEmitClearGrantOpen =
            rx.p25SessionState.sustain.hadSuccessfulEmit &&
            explicitClearGrantForCall &&
            !out.phase2TargetEssEncrypted &&
            !out.phase2WrongSlot &&
            selectedSlotEpochForFeed &&
            !dualSlotUntrustedNowEffective;
        const bool clearGrantMacOpen =
            explicitClearGrantForCall &&
            !out.phase2WrongSlot &&
            !out.phase2TargetEssEncrypted &&
            selectedSlotEpochForFeed &&
            (out.phase2TargetMacCrcValid ||
             out.phase2MacCrcValid > 0 ||
             burst.macCrcValid ||
             burst.macCrcLock ||
             recentMacEvidenceForCall);
        const bool securityProvedClearForFeed =
            establishedClearCall ||
            explicitClearGrantHardVoiceRelease ||
            sameCallClearSustainFeed ||
            p25Phase2TargetHardClearEvidence(out) ||
            (burst.essKnown && !burstEncryptedForFollowedCall && burst.sessionAudioRelease) ||
            // Window-level ESS already clear on the followed call (log: ess=clear).
            (out.phase2TargetEssKnown && !out.phase2TargetEssEncrypted && !out.phase2WrongSlot) ||
            clearLatchOpen ||
            postEmitClearGrantOpen ||
            clearGrantMacOpen ||
            selectedPostEmitContinuationForBurst ||
            (p25Phase2SessionSpeakerSustainActive(rx) && selectedSlotEpochForFeed && !dualSlotUntrustedNowEffective);
        // Selected-slot continuous clear: once traffic ESS/PTT (or established
        // same-call clear) is known, keep feeding descrambled Voice2/4 on the
        // grant slot every hop with a hard or carried selected-slot epoch; never
        // feed dual-slot-untrusted windows.
        const bool continuousSelectedClearFeed =
            currentBurstFeedTrusted &&
            securityProvedClearForFeed &&
            effectiveBurstSlot == followedGrantSlot &&
            !out.phase2WrongSlot &&
            !burstEncryptedForFollowedCall &&
            burst.xorMaskApplied &&
            selectedSlotEpochForFeed &&
            !dualSlotUntrustedNowEffective &&
            (establishedClearCall ||
             sameCallClearSustainFeed ||
             explicitClearGrantHardVoiceRelease ||
             forceEstablishedFeed ||
             p25Phase2TargetHardClearEvidence(out) ||
             (out.phase2TargetEssKnown && !out.phase2TargetEssEncrypted) ||
              p25Phase2SessionSpeakerSustainActive(rx) ||
              clearLatchOpen ||
              postEmitClearGrantOpen ||
              clearGrantMacOpen ||
              selectedPostEmitContinuationForBurst ||
              (rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted &&
               (burst.maskPhaseLock || burst.superframeLock ||
                out.phase2SuperframeBursts > 0)));
        const bool immediateAmbeDecodeAllowed =
            continuousSelectedClearFeed ||
            (!dualSlotUntrustedNowEffective &&
             currentBurstFeedTrusted &&
             securityProvedClearForFeed &&
             maskPhaseTrusted &&
             (effectiveVoiceReleaseTrusted ||
              (out.phase2TargetEssKnown && !out.phase2TargetEssEncrypted) ||
              (burst.essKnown && !burstEncryptedForFollowedCall && burst.sessionAudioRelease) ||
              sameCallClearSustainFeed ||
              p25Phase2EstablishedClearNoiseFeedAllowed(rx, out, burst, recentMacEvidenceForCall)));
        const bool queueUnknownAmbe =
            !immediateAmbeDecodeAllowed &&
            !rx.p25Phase2CallHadSpeakerAudio &&
            !rx.p25SessionState.sustain.hadSuccessfulEmit &&
            grantMayProbeVoice &&
            audioKey.valid() &&
            burst.grantSlotKnown &&
            burst.xorMaskApplied &&
            !burstEncryptedForFollowedCall &&
            effectiveBurstSlot == followedGrantSlot;

        if (immediateAmbeDecodeAllowed && !drainedPendingRawVoice) {
            if (burst.securityStateFromPtt) {
                p25Phase2HandlePttStartForPendingQueue(rx, audioKey);
            }
            // canDrain lives inside drainPendingRawVoice — do not force
            // drainedPendingRawVoice when dual-slot/live-preferred blocks.
            drainPendingRawVoice();
        }

        for (const auto& codeword : burst.voiceCodewords) {
            const bool codewordAbsKnown = codewordAbsoluteDibitKnown(codeword);
            const uint64_t codewordAbsDibit = codewordAbsoluteDibit(codeword);
            const uint64_t codewordEndAbsDibit = codewordAbsKnown ? codewordAbsDibit + 36u : 0u;
            P25Phase2AmbeValidationFrame frame;
            frame.burstDibitOffset = burst.dibitOffset;
            frame.superframeBurstIndexKnown = burst.superframeBurstIndexKnown;
            frame.superframeBurstIndex = burst.superframeBurstIndex;
            frame.grantSlotKnown = true;
            frame.grantSlot = effectiveBurstSlot;
            frame.voiceIndex = codeword.voiceIndex;
            frame.haveAbsoluteDibits = codewordAbsKnown;
            frame.codewordAbsDibit = codewordAbsDibit;
            frame.codewordEndAbsDibit = codewordEndAbsDibit;
            frame.duplicateInSession = codeword.duplicateInSession;
            const P25Phase2AmbeInputQuality codewordInputQuality =
                p25Phase2AmbeInputQualityForCodeword(live, burst, codeword);
            p25Phase2ApplyAmbeInputQualityToValidationFrame(codewordInputQuality, frame);

            const bool codewordEndsBeforeFresh = codewordBeforeFresh(codewordAbsKnown, codewordEndAbsDibit);
            if (codewordEndsBeforeFresh) {
                ++out.phase2ContextVoiceCodewords;
            }
            // Do not hard-drop Voice2/4 that sit before the 80 ms context floor
            // when this call has never emitted. Capture 20260905_105622 after
            // DEC-0006: unique selected frames a prior 80 ms hop missed
            // (p2vcw=0) landed in the next eye's context and were discarded
            // with dup=0 (startMs=98294 ctxDrop=2 fed=0). ISS-0001, REQ-P2.1,
            // DEC-0007.
            // DEC-0010/0011 lock-only after emit starved that catch-up
            // (105622 duty 0.735→0.11). Overlap repeats on independent CQPSK
            // eyes (073304 seq=389/401) are de-duped by ISCH lattice identity
            // (slot, burstIndex, voiceIndex) with a 300 ms TTL (DEC-0013).

            // Keep P25LiveDecoder's session duplicate marker diagnostic-only here.
            if (codeword.duplicateInSession) {
                skippedDuplicateVoice = true;
            }
            const Phase2VoiceFrameKey frameKey =
                p25Phase2VoiceFrameKeyFromBurst(burst, codeword, effectiveBurstSlot);
            const bool protocolKeyed = p25Phase2VoiceFrameKeyHasProtocolIdentity(frameKey);
            // SDRTrunk/OP25 stream already-selected timeslot frames by protocol
            // order.  The sequencer still owns Voice2/Voice4 order inside a
            // window.  Absolute recovered dibit position is what stops
            // overlapping GUI windows from replaying the same speech when the
            // eye is contiguous. Independent block-channelize eyes also need
            // the ISCH lattice key (DEC-0013).
            const bool contextAudioLockedOut =
                codewordEndsBeforeFresh &&
                !p25Phase2ShouldEmitAmbeFrame(rx, codewordAbsDibit, codewordEndAbsDibit,
                                             codewordAbsKnown, false);
            if (contextAudioLockedOut) {
                skippedDuplicateVoice = true;
                ++out.phase2DuplicateSuppressedVoiceCodewords;
                ++out.phase2AbsoluteDuplicateSuppressedVoiceCodewords;
                ++out.phase2ContextSuppressedVoiceCodewords;
                frame.duplicateSuppressed = true;
                frame.duplicateSuppressedByAbsolute = true;
                frame.contextSuppressed = true;
                ambeFrames.push_back(frame);
                continue;
            }
            (void)p25Phase2SyncAmbeEmitDedupeCallContext(rx);
            // Same-stream-position guard as SDRTrunk's continuous timeslot
            // module.  Protocol keys still order Voice2/Voice4 inside a window;
            // they must not bypass abs de-dupe across overlapping GUI windows.
            if (!p25Phase2ShouldEmitAmbeFrame(rx, codewordAbsDibit, codewordEndAbsDibit,
                                             codewordAbsKnown, false)) {
                skippedDuplicateVoice = true;
                ++out.phase2DuplicateSuppressedVoiceCodewords;
                ++out.phase2AbsoluteDuplicateSuppressedVoiceCodewords;
                frame.duplicateSuppressed = true;
                frame.duplicateSuppressedByAbsolute = true;
                ambeFrames.push_back(frame);
                continue;
            }
            if (p25Phase2LatticeKeyAlreadyEmitted(rx, frameKey, codewordAbsDibit,
                                                 codewordAbsKnown, nowMs)) {
                skippedDuplicateVoice = true;
                ++out.phase2DuplicateSuppressedVoiceCodewords;
                frame.duplicateSuppressed = true;
                ambeFrames.push_back(frame);
                continue;
            }

            attemptedNewVoice = true;
            const size_t variantSlot = p25Phase2AmbeVariantVoiceSlotClamped(codeword.voiceIndex);
            frame.lockedVariantBefore = rx.p25Phase2PreferredAmbeVariantByVoiceIndex[variantSlot];
            const auto resolved = p25ResolvePhase2AmbeFrame(rx, codeword, out);
            std::array<uint8_t, 96> ambeFrame = resolved.frame;
            frame.variant = resolved.variant;
            frame.variantProbes = resolved.probes;
            frame.probeScore = resolved.probes.empty()
                ? 0.0
                : resolved.probes.back().score;
            frame.ambeBits = p25CompactBits(ambeFrame);
            frame.lockedVariantAfter = rx.p25Phase2PreferredAmbeVariantByVoiceIndex[variantSlot];

            bool allowAmbeFeed = immediateAmbeDecodeAllowed;
            if (!allowAmbeFeed) {
                if (queueUnknownAmbe) {
                    bool probeOk = false;
                    if (lateEntryAudioProbeAllowed ||
                        explicitClearGrantProbeAllowed ||
                        writePreGateValidation ||
                        p25Phase2DeepTraceEnabled()) {
                        probeOk = p25ProbePhase2AmbeFrameForDiagnostics(
                            diagnosticAmbeDecoder, ambeFrame, out, frame);
                        out.phase2CurrentProbePcmUsable = out.phase2CurrentProbePcmUsable || probeOk;
                        out.phase2UnknownProbeQualityOk =
                            out.phase2UnknownProbeQualityOk ||
                            out.phase2DiagnosticAmbeProbeAccepted >= kP25Phase2UnknownGrantAudioProbeMinFrames;
                        if (explicitClearGrantProbeAllowed && probeOk) {
                            out.phase2UnknownProbeBlockReason = "explicit-clear-grant-probe-accepted";
                        }
                    }
                    // Unknown-security AMBE is queued first. Diagnostic probes
                    // use a throwaway vocoder; persistent mbelib is fed only
                    // after target-slot PTT/ESS or retained clear traffic proof.
                    P25P2PendingAmbeFrame pending;
                    pending.ambe96 = ambeFrame;
                    pending.frameKey = frameKey;
                    pending.frameKeyValid = true;
                    pending.voiceIndex = codeword.voiceIndex;
                    pending.grantSlotKnown = true;
                    pending.grantSlot = effectiveBurstSlot;
                    pending.haveAbsoluteDibits = codewordAbsKnown;
                    pending.codewordAbsDibit = codewordAbsDibit;
                    pending.codewordEndAbsDibit = codewordEndAbsDibit;
                    p25Phase2ApplyAmbeInputQualityToPending(codewordInputQuality, pending);
                    if (p25QueuePhase2PendingAmbeFrame(rx, audioKey, pending)) {
                        ++out.phase2PendingAmbeFramesQueued;
                    }
                    queuedRawVoice = true;
                    ambeFrames.push_back(frame);
                    continue;
                } else {
                    ++out.phase2RejectedVoiceCodewords;
                    ambeFrames.push_back(frame);
                    continue;
                }
            }

            if (rx.p25Phase2TalkspurtEndedPendingVocoderReset && codewordAbsKnown &&
                rx.p25SessionState.talkspurtOrder.voiceAfterBoundary(codewordAbsDibit) &&
                p25Phase2ShouldEmitAmbeFrame(rx, codewordAbsDibit, codewordEndAbsDibit, true, false)) {
                p25Phase2ResetVocoderForNewTalkspurt(rx, "post-end-fresh-voice", nowMs);
            }
            P25Phase2SequencerSpeechInput seqInput;
            seqInput.key = frameKey;
            seqInput.ambe96 = ambeFrame;
            seqInput.haveAmbe = true;
            seqInput.grantSlotKnown = true;
            seqInput.grantSlot = effectiveBurstSlot;
            seqInput.haveAbsoluteDibits = codewordAbsKnown;
            seqInput.codewordAbsDibit = codewordAbsDibit;
            seqInput.codewordEndAbsDibit = codewordEndAbsDibit;
            p25Phase2ApplyAmbeInputQualityToSpeechInput(codewordInputQuality, seqInput);
            const auto readySpeech =
                p25Phase2SequencerProcessSpeechFrame(rx, seqInput, &out, outputRateHz);
            if (readySpeech.empty()) {
                skippedDuplicateVoice = true;
                ++out.phase2DuplicateSuppressedVoiceCodewords;
                ++out.phase2SequencerSuppressedVoiceCodewords;
                frame.duplicateSuppressed = true;
                frame.duplicateSuppressedBySequencer = true;
                ambeFrames.push_back(frame);
                continue;
            }
            std::vector<uint64_t> readyAbsStarts;
            for (const auto& speechItem : readySpeech) {
                P25Phase2AmbeValidationFrame speechFrame = frame;
                speechFrame.haveAbsoluteDibits = speechItem.haveAbsoluteDibits;
                speechFrame.codewordAbsDibit = speechItem.codewordAbsDibit;
                speechFrame.codewordEndAbsDibit = speechItem.codewordEndAbsDibit;
                const P25Phase2AmbeInputQuality speechInputQuality =
                    p25Phase2AmbeInputQualityFromSpeechInput(speechItem);
                p25Phase2ApplyAmbeInputQualityToValidationFrame(speechInputQuality, speechFrame);
                bool duplicateInReadyBatch = false;
                if (speechItem.haveAbsoluteDibits) {
                    duplicateInReadyBatch = std::any_of(
                        readyAbsStarts.begin(),
                        readyAbsStarts.end(),
                        [&](uint64_t prior) {
                            constexpr uint64_t kReadyBatchDuplicateToleranceDibits = 12u;
                            const uint64_t delta = speechItem.codewordAbsDibit > prior
                                ? speechItem.codewordAbsDibit - prior
                                : prior - speechItem.codewordAbsDibit;
                            return delta <= kReadyBatchDuplicateToleranceDibits;
                        });
                }
                if (duplicateInReadyBatch ||
                    !p25Phase2ShouldEmitAmbeFrame(rx,
                                                 speechItem.codewordAbsDibit,
                                                 speechItem.codewordEndAbsDibit,
                                                 speechItem.haveAbsoluteDibits,
                                                 false) ||
                    p25Phase2LatticeKeyAlreadyEmitted(rx, speechItem.key, speechItem.codewordAbsDibit,
                                                     speechItem.haveAbsoluteDibits, nowMs)) {
                    skippedDuplicateVoice = true;
                    ++out.phase2DuplicateSuppressedVoiceCodewords;
                    ++out.phase2AbsoluteDuplicateSuppressedVoiceCodewords;
                    speechFrame.duplicateSuppressed = true;
                    speechFrame.duplicateSuppressedByAbsolute = true;
                    ambeFrames.push_back(speechFrame);
                    continue;
                }
                if (speechItem.haveAbsoluteDibits) {
                    readyAbsStarts.push_back(speechItem.codewordAbsDibit);
                }

                p25Phase2FillFeedGapWithPlc(rx,
                                            out,
                                            outputRateHz,
                                            speechItem.codewordAbsDibit,
                                            speechItem.haveAbsoluteDibits);
                p25RecordPhase2AmbeFeedCadence(out,
                                               speechItem.codewordAbsDibit,
                                               speechItem.haveAbsoluteDibits);

                bool ok = p25DecodePhase2AmbeFrameToAudio(
                    rx, speechItem.ambe96, outputRateHz, out, speechFrame, speechInputQuality);
                if (!ok && speechFrame.variant >= 0) {
                    ++rx.p25Phase2PreferredAmbeVariantMissesByVoiceIndex[variantSlot];
                    p25Phase2RefreshAmbeVariantSummary(rx, variantSlot);
                } else if (ok && speechFrame.variant >= 0) {
                    ++rx.p25Phase2PreferredAmbeVariantHitsByVoiceIndex[variantSlot];
                    p25Phase2RefreshAmbeVariantSummary(rx, variantSlot);
                }
                if (ok || speechFrame.timelineEmitted) {
                    p25Phase2RememberEmittedAmbeFrame(rx, speechItem.codewordAbsDibit, speechItem.codewordEndAbsDibit,
                                                      speechItem.haveAbsoluteDibits,
                                                      &speechItem.key,
                                                      nowMs);
                }
                if (ok || speechFrame.timelineEmitted) {
                    p25Phase2RecordEmittedSpeechOrdinal(out, speechItem);
                }
                if (ok) {
                    acceptedVoice = true;
                    if (lateEntryStrongTargetReleaseAllowed) {
                        lateEntryStrongTargetReleaseDecoded = true;
                    } else {
                        acceptedReleaseVoice = true;
                    }
                }
                ambeFrames.push_back(speechFrame);
            }

            if (!drainedPendingRawVoice) {
                if (burst.securityStateFromPtt) {
                    p25Phase2HandlePttStartForPendingQueue(rx, audioKey);
                }
                drainPendingRawVoice();
            }
        }
    }

    if (lateEntryStrongTargetReleaseDecoded &&
        establishedClearCall &&
        p25Phase2StrongVoiceTimeslotPcm(out)) {
        sdrtrunkLateEntryVoiceRelease = true;
        out.phase2SdrtrunkLateEntryVoiceRelease = true;
        out.phase2CurrentProbePcmUsable = true;
        out.phase2UnknownProbeQualityOk = true;
        out.phase2UnknownProbeBlockReason = "late-entry-strong-target-release";
        acceptedReleaseVoice = true;
    }

    releasePendingRawVoiceFromEss();
    releasePendingRawVoiceFromTrustedTrafficState();
    releasePendingRawVoiceFromExplicitClearTrafficProof();
    keepUnknownGrantProbeDiagnosticOnly();
    // Keep pacing leftover late-entry AMBE after the first clear proof window.
    // Rate-limited drain leaves frames armed; empty/wrong-slot ticks must still
    // drip them so the speaker does not go silent mid-call.
    if (audioKey.valid() &&
        !drainedPendingRawVoice &&
        establishedClearCall &&
        canDrainPendingRawVoiceThisWindow() &&
        !rx.p25VoiceEncrypted &&
        !out.phase2TargetEssEncrypted &&
        p25Phase2PendingAudioMatches(rx, audioKey) &&
        p25Phase2PendingAmbeFrameCount(rx, audioKey) > 0) {
        drainPendingRawVoice();
    }
    // If live selected VCWs own this window, drop leftover late-entry stash
    // so the next tick cannot concatenate time-displaced AMBE (dual-voice).
    if (!drainedPendingRawVoice) {
        discardStalePendingWhenLivePreferred();
    }

    if (out.phase2AmbeVariantChanges > 0) {
        // Variant churn is diagnostic only.  Never reconstruct the persistent
        // vocoder mid-call for layout thrash — that created blocky islands.
        out.phase2AmbeVariantUnstable = false;
        (void)p25Phase2AmbeVariantInstabilityFatal(out);
    }

    if (acceptedReleaseVoice &&
        (establishedClearCall ||
         p25Phase2TargetHardClearEvidence(out) ||
         sdrtrunkLateEntryVoiceRelease ||
         out.phase2ExplicitClearGrantVoiceRelease)) {
        if (out.phase2ExplicitClearGrantVoiceRelease) {
            p25RefreshPhase2RecentSecurityEvidence(
                rx,
                audioKey,
                nowMs,
                out.phase2TargetMacCrcValid,
                out.phase2MacCrcValid > 0,
                out.phase2TargetEssKnown,
                out.phase2TargetEssEncrypted,
                out.phase2TargetSessionAudioRelease,
                out.phase2TargetSecurityStateFromPtt,
                out.phase2SuperframeBursts > 0 && out.phase2MaskedBursts > 0);
        }
        // Target-slot PTT/ESS or preserved target-slot clear-call state has
        // established playable clear audio for this followed slot. Now clear the
        // acquisition/probe flags that would otherwise mute the queued/current
        // speaker block.
        out.phase2AudioLockMissing = false;
        out.phase2MetadataMissing = false;
        out.phase2MaskMissing = false;
        out.phase2MaskAppliedNoMacCrc = false;
        out.phase2EssMissing = false;
        out.phase2WrongSlot = false;
        out.phase2WrongSlotVoiceCodewords = 0;
        // Rejected candidate AMBE frames in the same overlapped/mixed window are
        // diagnostic only once at least one followed-slot AMBE frame decoded to
        // sane PCM.  Do not let one rejected candidate mute a good block.
        out.phase2AmbeRejected = false;
        out.phase2LateEntryWaiting = false;
        out.phase2VoiceUnsupported = false;
    } else if ((sdrtrunkLateEntryVoiceRelease ||
                p25Phase2TargetHardClearEvidence(out)) &&
               out.decodedFrames > 0 && !out.audio.empty() && !out.skippedEncrypted) {
        // Safety net for already-trusted clear audio: once security was proven
        // before mbelib was fed, do not let stale acquisition flags mute the
        // current speaker block.
        out.phase2AudioLockMissing = false;
        out.phase2MetadataMissing = false;
        out.phase2MaskMissing = false;
        out.phase2MaskAppliedNoMacCrc = false;
        out.phase2EssMissing = false;
        out.phase2LateEntryWaiting = false;
        out.waitingForClearGrant = false;
        if (out.diag == P25VoiceDiagCode::Idle || out.diag == P25VoiceDiagCode::WaitingForClearGrant) {
            out.diag = P25VoiceDiagCode::Decoding;
        }
    } else if (queuedRawVoice && !recentClearSecurityForCall) {
        // Late-entry voice is present and descrambled, but the target call has
        // not produced target-slot PTT/ESS or another trusted clear release yet.
        // Match sdrtrunk: hold raw voice frames and keep diagnostics explicit so
        // this is not mistaken for a vocoder failure.
        out.phase2AudioLockMissing = true;
        out.phase2MetadataMissing = true;
        out.phase2EssMissing = true;
        out.phase2LateEntryWaiting = true;
        out.waitingForClearGrant = true;
    } else if (acceptedVoice) {
        out.phase2AudioLockMissing = true;
        out.phase2MetadataMissing = true;
        out.phase2LateEntryWaiting = true;
    }

    if (sawVoice && skippedDuplicateVoice && !attemptedNewVoice && !acceptedVoice) {
        out.diag = P25VoiceDiagCode::Decoding;
        writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
        return out;
    }
    if (sawVoice && !acceptedVoice) {
        if (out.skippedEncrypted) {
            out.diag = P25VoiceDiagCode::SkippedEncrypted;
            writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
            return out;
        }
        if (queuedRawVoice) {
            // Only report "waiting clear grant" when security is actually unknown.
            // Field logs mislabeled clear grants that already had mask/VCW but no
            // usable AMBE as "waiting clear grant", hiding the real failure mode.
            out.diag = explicitClearGrantForCall
                ? P25VoiceDiagCode::Phase2LateEntryWaiting
                : P25VoiceDiagCode::WaitingForClearGrant;
            writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
            return out;
        }
        const bool trafficClearTrusted =
            establishedClearCall ||
            sdrtrunkLateEntryVoiceRelease ||
            p25Phase2TargetHardClearEvidence(out);
        if (rx.p25VoiceMaskParamsKnown && out.phase2MaskedBursts > 0 && out.phase2MacCrcValid == 0 && !out.phase2EssKnown) {
            out.phase2MaskAppliedNoMacCrc = true;
        }
        if (!out.phase2EssKnown && rx.p25VoiceMaskParamsKnown && out.phase2MaskedBursts > 0 && !trafficClearTrusted) {
            out.phase2EssMissing = true;
            out.phase2LateEntryWaiting = true;
            out.phase2MetadataMissing = true;
        } else if (rx.p25VoiceMaskParamsKnown && out.phase2MaskedBursts == 0) {
            out.phase2MaskMissing = true;
        } else if (!rx.p25VoiceMaskParamsKnown) {
            out.phase2MetadataMissing = true;
        }
        // Clear grant with VCWs that never produce usable AMBE is an AMBE/mask-phase
        // problem, not "waiting for clear grant".  Surface the accurate diagnostic.
        if (attemptedNewVoice && !acceptedVoice) {
            out.phase2AmbeRejected = true;
        }
        if (!trafficClearTrusted) {
            out.phase2VoiceUnsupported = true;
        }
        out.waitingForClearGrant = false;
    }
    out.diag = chooseP25VoiceDiag(out);
    writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
    return out;
}


P25VoiceAudioBlock decodeP25Phase1VoiceBlock(Receiver& rx,
                                                    const P25LiveDecodeResult& live,
                                                    P25VoiceAudioBlock out,
                                                    double outputRateHz)
{
    // Production gate: do not let a Phase 1 IMBE false positive open audio while
    // the receiver is actually seeing Phase 2/CQPSK control or TDMA fragments.
    // This is the common source of the one-second white-noise blip during
    // frequency/voice-follow transitions.
    if (!out.nidLock || out.phase2Bursts > 0 || out.phase2VoiceCodewords > 0) {
        if (out.phase2Bursts > 0 || out.phase2VoiceCodewords > 0) out.phase2VoiceUnsupported = true;
        out.diag = chooseP25VoiceDiag(out);
        return out;
    }
    for (const auto& frame : live.imbeFrames) {
        if (!frame.valid) continue;
        const auto decoded = rx.p25ImbeVoiceDecoder.decodeImbe4400Frame(frame.imbe88);
        if (decoded.status != P25VoiceDecodeStatus::Decoded || decoded.pcm.empty()) continue;
        auto block = resampleDecodedP25Pcm(rx, decoded.pcm, decoded.sampleRate, outputRateHz);
        out.audio.insert(out.audio.end(), block.begin(), block.end());
        ++out.decodedFrames;
    }
    out.diag = chooseP25VoiceDiag(out);
    return out;
}


P25VoiceAudioBlock decodeP25VoiceAudioBlock(Receiver& rx,
                                                   const std::vector<std::complex<float>>& iq,
                                                   double sampleRateHz,
                                                   double centerFreqHz,
                                                   double targetFreqHz,
                                                   double outputRateHz,
                                                   uint64_t iqStartAbsolute,
                                                   bool iqStartAbsoluteKnown,
                                                   size_t contextIqSamples)
{
    P25VoiceAudioBlock out;
    out.talkgroupId = rx.p25VoiceTalkgroupId;
    out.centerFreqHz = centerFreqHz;
    out.effectiveTargetFreqHz = targetFreqHz;
    out.backendAvailable = rx.p25VoicePhase2
        ? rx.p25AmbeVoiceDecoder.backendAvailable()
        : rx.p25ImbeVoiceDecoder.backendAvailable();
    if (!rx.p25VoiceDecodeEnabled || iq.empty()) {
        out.diag = P25VoiceDiagCode::Idle;
        return out;
    }
    if (rx.p25VoiceEncrypted && !rx.p25VoicePhase2) {
        out.skippedEncrypted = true;
        out.diag = P25VoiceDiagCode::SkippedEncrypted;
        return out;
    }
    if (!rx.p25VoiceClearKnown && !rx.p25VoicePhase2) {
        out.waitingForClearGrant = true;
        out.diag = P25VoiceDiagCode::WaitingForClearGrant;
        return out;
    }

    const bool retunedPhase2Traffic = rx.p25VoicePhase2 && rx.p25TrafficRetunesPrimary;
    const bool oneRtlPhysicallyOnVoice =
        retunedPhase2Traffic &&
        rx.p25IndependentTrafficSource &&
        std::isfinite(centerFreqHz) && centerFreqHz > 0.0 &&
        std::isfinite(targetFreqHz) && targetFreqHz > 0.0 &&
        std::abs(centerFreqHz - targetFreqHz) <= 50.0;
    const bool oneRtlLowIfTrafficSource =
        retunedPhase2Traffic &&
        rx.p25IndependentTrafficSource &&
        std::isfinite(centerFreqHz) && centerFreqHz > 0.0 &&
        std::isfinite(targetFreqHz) && targetFreqHz > 0.0 &&
        std::abs(centerFreqHz - targetFreqHz) > 50.0;
    const bool verifiedTrafficTargetOffset =
        rx.p25Phase2TrafficTargetOffsetKnown &&
        rx.p25Phase2TrafficTargetOffsetTrust >= kP25Phase2TrafficTargetOffsetVerifiedTrust &&
        rx.p25Phase2TrafficTargetOffsetMisses == 0;
    const double trafficOffsetHz = p25Phase2EffectiveTrafficTargetOffsetHz(rx);
    double effectiveTargetFreqHz = targetFreqHz;
    if (retunedPhase2Traffic && verifiedTrafficTargetOffset && trafficOffsetHz != 0.0) {
        const double lockedTargetHz = targetFreqHz + trafficOffsetHz;
        if (p25Phase2TargetInSamplePassband(sampleRateHz, centerFreqHz, lockedTargetHz)) {
            effectiveTargetFreqHz = lockedTargetHz;
        }
    } else if (retunedPhase2Traffic && trafficOffsetHz != 0.0 &&
               !verifiedTrafficTargetOffset && !oneRtlLowIfTrafficSource) {
        // Unverified soft-AFC / probe seed: bias only true same-center traffic.
        // Low-IF one-RTL follows already have an independent traffic source
        // center; carrying CC AFC straight into the voice target turned
        // 418.05000 MHz grants into 418.05125 MHz workers in field captures.
        // Treat that Hz as an ordered probe candidate until traffic evidence
        // promotes it.
        const double seededTargetHz = targetFreqHz + trafficOffsetHz;
        if (p25Phase2TargetInSamplePassband(sampleRateHz, centerFreqHz, seededTargetHz)) {
            effectiveTargetFreqHz = seededTargetHz;
        }
    }
    const double initialEffectiveTargetFreqHz = effectiveTargetFreqHz;
    const auto phase2VoiceDecodeStarted = std::chrono::steady_clock::now();
    const bool coldAcquireWindow =
        retunedPhase2Traffic &&
        contextIqSamples == 0 &&
        !p25Phase2SessionHasHardTargetAcquire(rx);
    // One-RTL physical retune starts at exact granted MHz with no verified
    // traffic offset.  Field capture 20260710_043237 showed p2bursts=0 for the
    // entire clear TG30304 follow because acquisitionOffsetProbe was disabled
    // whenever the tuner was already on voice, and NeedsTargetOffsetProbe also
    // requires some TDMA telemetry first.  Allow a bounded cold probe from the
    // granted center so nearby eyes can be tried before the ACQ watchdog fires.
    const bool coldOneRtlZeroBurstAcquire =
        oneRtlPhysicallyOnVoice &&
        !p25Phase2SessionHasHardTargetAcquire(rx) &&
        rx.p25VoiceDiagnostics.phase2Bursts == 0 &&
        rx.p25VoiceDiagnostics.phase2MaskedBursts == 0 &&
        rx.p25VoiceDiagnostics.decodedFrames == 0 &&
        !verifiedTrafficTargetOffset;
    // Soft PPM carry for cold one-RTL: same RTL crystal error that pulled the
    // control channel ~0.8–1.25 kHz also applies after physical retune.  Do not
    // lock this offset; only bias the first pass so CQPSK can see the eye before
    // the ACQ watchdog.  Capture 20260712_014121: clear TG10609 @ 420.725 stayed
    // at offset=0.0kHz with p2bursts=0 while worker-busy starved later probes.
    bool softAfcColdSeedApplied = false;
    if (coldOneRtlZeroBurstAcquire &&
        std::isfinite(rx.p25FrozenAfcOffsetHz) &&
        std::abs(rx.p25FrozenAfcOffsetHz) >= 200.0 &&
        std::abs(rx.p25FrozenAfcOffsetHz) <= 2500.0) {
        const double softTargetHz = targetFreqHz + rx.p25FrozenAfcOffsetHz;
        if (p25Phase2TargetInSamplePassband(sampleRateHz, centerFreqHz, softTargetHz)) {
            effectiveTargetFreqHz = softTargetHz;
            softAfcColdSeedApplied = true;
        }
    }
    // Soft-AFC biases pass 1 only; later zero-burst jobs must run the bounded
    // ±1250/±2500 probe.  Use offset-miss counter so deferral is one-shot per follow.
    const bool deferOffsetProbeForSoftAfcPass =
        softAfcColdSeedApplied &&
        rx.p25Phase2TrafficTargetOffsetMisses <= 0;
    const bool acquisitionOffsetProbe =
        retunedPhase2Traffic &&
        !deferOffsetProbeForSoftAfcPass &&
        !p25Phase2SessionHasHardTargetAcquire(rx) &&
        rx.p25VoiceDiagnostics.phase2MaskedBursts == 0 &&
        rx.p25VoiceDiagnostics.decodedFrames == 0 &&
        // First cold eye is soft-AFC / seeded offset only — run ±1250 probes on
        // the next job so we don't turn the first contiguous window into a
        // multi-processIq monolith (SDRTrunk streams; we stagger acquire).
        rx.p25SessionState.sustain.coldAcquirePasses >= 1 &&
        (coldOneRtlZeroBurstAcquire ||
         (!oneRtlPhysicallyOnVoice &&
          !coldAcquireWindow &&
          rx.p25Phase2TrafficTargetOffsetKnown &&
          rx.p25Phase2TrafficTargetOffsetTrust < kP25Phase2TrafficTargetOffsetVerifiedTrust));
    if (retunedPhase2Traffic &&
        !p25Phase2SessionHasHardTargetAcquire(rx)) {
        ++rx.p25SessionState.sustain.coldAcquirePasses;
        if (rx.p25SessionState.sustain.coldAcquirePasses > 1000000) {
            rx.p25SessionState.sustain.coldAcquirePasses = 1000000;
        }
    }
    const int offsetProbeBudgetMs = acquisitionOffsetProbe
        ? kP25Phase2TrafficOffsetProbeAcquireBudgetMs
        : kP25Phase2TrafficOffsetProbeBudgetMs;
    const size_t offsetProbeMaxCandidates = acquisitionOffsetProbe
        ? kP25Phase2TrafficOffsetProbeAcquireMaxCandidates
        : kP25Phase2TrafficOffsetProbeMaxCandidates;
    auto phase2VoiceDecodeBudgetExceeded = [&]() {
        if (!rx.p25VoicePhase2) return false;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - phase2VoiceDecodeStarted).count();
        return elapsed >= offsetProbeBudgetMs;
    };
    syncP25Phase2MaskParametersToLiveDecoder(rx);
    if (rx.p25VoicePhase2) {
        rx.p25VoiceLiveDecoder.setAllowPhase2SoftAmbeMaskPhaseLock(false);
        rx.p25VoiceLiveDecoder.setPhase2PreferredTdmaSlot(
            rx.p25VoiceTdmaSlotKnown, rx.p25VoiceTdmaSlot);
        if (iqStartAbsoluteKnown &&
            std::isfinite(sampleRateHz) && sampleRateHz > 0.0 &&
            !rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc) {
            const double streamSymbolRate = rx.p25VoiceLiveDecoder.config().symbolRate > 0.0
                ? rx.p25VoiceLiveDecoder.config().symbolRate
                : 6000.0;
            const uint64_t chunkStartAbsDibit = static_cast<uint64_t>(
                std::llround(static_cast<long double>(iqStartAbsolute) *
                             static_cast<long double>(streamSymbolRate) /
                             static_cast<long double>(sampleRateHz)));
            const size_t chunkDibits = static_cast<size_t>(std::max<long long>(
                1,
                std::llround(static_cast<long double>(iq.size()) *
                             static_cast<long double>(streamSymbolRate) /
                             static_cast<long double>(sampleRateHz))));
            rx.p25VoiceLiveDecoder.alignPhase2AbsoluteDibitCursor(chunkStartAbsDibit, chunkDibits);
        }
    }
    auto live = rx.p25VoiceLiveDecoder.processIq(iq, sampleRateHz, centerFreqHz, effectiveTargetFreqHz);

    // Big-ticket Phase 2 recovery: if the first pass has Phase-2 telemetry but
    // no MAC/ESS, try a small set of target offsets from the same decoder state
    // and keep the candidate that produces the strongest metadata/voice evidence.
    // This covers the field case where the RF was centred on the traffic channel
    // but a frozen control-channel AFC offset moved the channelizer several kHz
    // away from the correct ACCH/AMBE eye.  sdrtrunk's traffic source is already
    // centred independently; this makes our scanner-follow path less brittle.
    const bool stickyOffsetNeedsRefresh =
        retunedPhase2Traffic &&
        verifiedTrafficTargetOffset &&
        !p25Phase2TrafficTargetOffsetEvidenceStrong(rx, live) &&
        (p25Phase2LiveHasRetuneProbeTelemetry(live) ||
         rx.p25Phase2TrafficTargetOffsetMisses < 2);
    const bool unverifiedSeedNeedsProbe =
        retunedPhase2Traffic &&
        rx.p25Phase2TrafficTargetOffsetKnown &&
        !verifiedTrafficTargetOffset &&
        !p25Phase2TrafficTargetOffsetEvidenceStrong(rx, live) &&
        live.stats.phase2MaskedBursts == 0;
    const bool skipOffsetProbeAfterMaskLock =
        p25Phase2SessionHadBurstEye(rx) ||
        live.stats.phase2MaskedBursts >= 1 ||
        live.stats.phase2SuperframeBursts >= 2 ||
        live.stats.phase2Bursts >= 1 ||
        rx.p25VoiceDiagnostics.phase2MaskedBursts >= 1 ||
        rx.p25SessionState.sustain.hadBootstrapMaskLock ||
        rx.p25SessionState.sustain.hadSuccessfulEmit;
    const bool lockedOffsetStrong =
        retunedPhase2Traffic &&
        rx.p25Phase2TrafficTargetOffsetKnown &&
        p25Phase2TrafficTargetOffsetEvidenceStrong(rx, live);
    const auto currentFollowedSlotEvidence = p25Phase2FollowedSlotEvidenceForReceiver(rx, live);
    const bool grantedTargetInPassband =
        p25Phase2TargetInSamplePassband(sampleRateHz, centerFreqHz, targetFreqHz);
    const bool activeFollowedSlotDecodePresent =
        currentFollowedSlotEvidence.targetVoiceCodewords > 0 ||
        currentFollowedSlotEvidence.targetMaskedBursts >= 2 ||
        (live.stats.phase2SuperframeBursts >= 6 && live.stats.phase2MaskedBursts >= 6);
    const bool suppressOffsetProbeForActiveTraffic =
        grantedTargetInPassband &&
        rx.p25VoiceClearKnown &&
        !rx.p25VoiceEncrypted &&
        rx.p25VoiceMaskParamsKnown &&
        activeFollowedSlotDecodePresent;
    const bool skipOffsetProbeIndependentTraffic =
        oneRtlPhysicallyOnVoice &&
        !acquisitionOffsetProbe &&
        (skipOffsetProbeAfterMaskLock || lockedOffsetStrong);
    // If the baseline pass already burned most of a realtime slot, more offset
    // probes only widen worker-busy gaps.  Prefer the next fresh IQ window.
    const auto baselineElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - phase2VoiceDecodeStarted).count();
    const bool skipOffsetProbeToProtectCadence =
        acquisitionOffsetProbe &&
        oneRtlPhysicallyOnVoice &&
        baselineElapsedMs >= 80 &&
        live.stats.phase2Bursts == 0 &&
        live.stats.phase2VoiceCodewords == 0 &&
        live.stats.phase2MacPdus == 0;
    if (kP25Phase2TrafficTargetOffsetProbeEnabled &&
        !skipOffsetProbeIndependentTraffic &&
        !skipOffsetProbeToProtectCadence &&
        retunedPhase2Traffic &&
        !suppressOffsetProbeForActiveTraffic &&
        !skipOffsetProbeAfterMaskLock &&
        !phase2VoiceDecodeBudgetExceeded() &&
        (p25Phase2NeedsTargetOffsetProbe(live) ||
         stickyOffsetNeedsRefresh ||
         unverifiedSeedNeedsProbe ||
         acquisitionOffsetProbe) &&
        !(lockedOffsetStrong && !stickyOffsetNeedsRefresh && !unverifiedSeedNeedsProbe && !acquisitionOffsetProbe)) {
        // Clone only when a probe will actually run — avoid pointless copy on
        // every sustain/cold soft-AFC pass.
        P25LiveDecoder baselineDecoder = rx.p25VoiceLiveDecoder.createIndependentProbeCopy(true);
        std::vector<double> targetCandidates;
        const double base = (std::isfinite(rx.freqHz) && rx.freqHz > 0.0) ? rx.freqHz : targetFreqHz;
        const bool preferNominalBeforeUnverifiedOffset =
            oneRtlLowIfTrafficSource && !verifiedTrafficTargetOffset;
        if (preferNominalBeforeUnverifiedOffset) {
            p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz);
            p25AddUniqueTargetCandidate(targetCandidates, effectiveTargetFreqHz);
        }
        // Prefer control-AFC / PPM hints before the coarse grid so the short
        // acquire candidate budget actually evaluates the likely eye first.
        if (std::isfinite(rx.p25FrozenAfcOffsetHz) &&
            std::abs(rx.p25FrozenAfcOffsetHz) >= 50.0 &&
            std::abs(rx.p25FrozenAfcOffsetHz) <= 45000.0) {
            p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz + rx.p25FrozenAfcOffsetHz);
            p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz - rx.p25FrozenAfcOffsetHz);
            const double snapHz = std::round(rx.p25FrozenAfcOffsetHz / 1250.0) * 1250.0;
            if (std::abs(snapHz) >= 50.0) {
                p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz + snapHz);
                p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz - snapHz);
            }
            p25AddUniqueTargetCandidate(targetCandidates, base + rx.p25FrozenAfcOffsetHz);
            p25AddUniqueTargetCandidate(targetCandidates, base - rx.p25FrozenAfcOffsetHz);
        }
        if (!preferNominalBeforeUnverifiedOffset) {
            p25AddUniqueTargetCandidate(targetCandidates, effectiveTargetFreqHz);
            p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz);
        }
        p25AddUniqueTargetCandidate(targetCandidates, rx.freqHz);
        p25AddUniqueTargetCandidate(targetCandidates, centerFreqHz);
        if (rx.p25Phase2TrafficTargetOffsetKnown) {
            const double lockedOffsetHz = p25Phase2EffectiveTrafficTargetOffsetHz(rx);
            if (lockedOffsetHz != 0.0) {
                p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz + lockedOffsetHz);
            }
        }
        constexpr std::array<double, 10> kOffsetProbeHz{
            -1250.0, 1250.0,
            -2500.0, 2500.0,
            -3750.0, 3750.0,
            -5000.0, 5000.0,
            -7500.0, 7500.0,
        };
        for (double off : kOffsetProbeHz) p25AddUniqueTargetCandidate(targetCandidates, base + off);
        if (std::isfinite(targetFreqHz) && targetFreqHz > 0.0 && std::abs(targetFreqHz - base) >= 100.0) {
            for (double off : kOffsetProbeHz) p25AddUniqueTargetCandidate(targetCandidates, targetFreqHz + off);
        }

        int bestScore = p25Phase2LiveAudioRecoveryScore(rx, live);
        P25LiveDecoder bestDecoder = rx.p25VoiceLiveDecoder;
        const auto startEv = p25Phase2FollowedSlotEvidenceForReceiver(rx, live);
        const bool logOffsetProbe = p25Phase2OffsetProbeLogAllowed(acquisitionOffsetProbe);
        if (logOffsetProbe) {
            spdlog::info("P25 P2 traffic offset probe start: nominal={} effective={} off={} path={} trust={} misses={} score={} strong={} targetVcw={} targetMask={} targetSf={} mac={} cold={}",
                         targetFreqHz, effectiveTargetFreqHz, effectiveTargetFreqHz - targetFreqHz,
                         live.stats.demodPath,
                         rx.p25Phase2TrafficTargetOffsetTrust, rx.p25Phase2TrafficTargetOffsetMisses,
                         bestScore, p25Phase2TrafficTargetOffsetEvidenceStrong(rx, live),
                         startEv.targetVoiceCodewords, startEv.targetMaskedBursts, startEv.targetSuperframeBursts,
                         live.stats.phase2MacCrcValid,
                         acquisitionOffsetProbe);
        }
        size_t offsetProbeCandidates = 0;
        for (double candidateTarget : targetCandidates) {
            if (offsetProbeCandidates >= offsetProbeMaxCandidates ||
                phase2VoiceDecodeBudgetExceeded()) {
                break;
            }
            if (std::abs(candidateTarget - effectiveTargetFreqHz) < 50.0) continue;
            if (!p25Phase2TargetInSamplePassband(sampleRateHz, centerFreqHz, candidateTarget)) {
                continue;
            }
            ++offsetProbeCandidates;
            P25LiveDecoder candidateDecoder = baselineDecoder;
            if (acquisitionOffsetProbe) {
                // Keep probe passes cheap so several nearby eyes fit the acquire
                // budget; restore full CQPSK budget if this candidate wins below.
                candidateDecoder.setMaxCqpskSearchCandidates(24);
                candidateDecoder.setRealtimeDecodeBudgetMs(70);
            }
            auto candidateLive = candidateDecoder.processIq(iq, sampleRateHz, centerFreqHz, candidateTarget);
            const int candidateScore = p25Phase2LiveAudioRecoveryScore(rx, candidateLive);
            const bool candidateStrong = p25Phase2TrafficTargetOffsetEvidenceStrong(rx, candidateLive);
            const bool bestStrong = p25Phase2TrafficTargetOffsetEvidenceStrong(rx, live);
            const auto candidateEv = p25Phase2FollowedSlotEvidenceForReceiver(rx, candidateLive);
            const bool candidateFollowedSlotTelemetry =
                !candidateEv.slotKnown ||
                candidateEv.targetVoiceCodewords > 0 ||
                candidateEv.targetMaskedBursts > 0 ||
                candidateEv.targetSuperframeBursts > 0 ||
                candidateEv.targetMacCrcValid > 0 ||
                candidateEv.targetIschDecoded > 0 ||
                candidateEv.targetEssKnown ||
                candidateEv.targetSessionAudioRelease;
            const bool candidateContradictsTrustedClear =
                rx.p25VoiceClearKnown &&
                !rx.p25VoiceEncrypted &&
                candidateLive.stats.phase2EssKnown &&
                candidateLive.stats.phase2EssEncrypted;
            const bool candidateIsNominal = std::abs(candidateTarget - targetFreqHz) < 50.0;
            const bool candidateIsSeed =
                rx.p25Phase2TrafficTargetOffsetKnown &&
                std::abs(candidateTarget - (targetFreqHz + p25Phase2EffectiveTrafficTargetOffsetHz(rx))) < 50.0;
            const bool unverifiedSeedHasUsefulTelemetry =
                candidateIsSeed &&
                !verifiedTrafficTargetOffset &&
                candidateFollowedSlotTelemetry &&
                p25Phase2LiveHasRetuneProbeTelemetry(candidateLive);
            // Cold acquire previously required "strong" evidence before keeping a
            // non-nominal offset, so every weak-but-real burst/MAC eye was thrown
            // away and the probe could never climb out of p2bursts=0.
            const bool coldAcquireUsefulTelemetry =
                acquisitionOffsetProbe &&
                candidateFollowedSlotTelemetry &&
                p25Phase2LiveHasRetuneProbeTelemetry(candidateLive);
            if (logOffsetProbe) {
                spdlog::info("P25 P2 traffic offset probe candidate: candidate={} off={} path={} score={} strong={} seed={} nominal={} followedSlot={} coldUseful={} targetVcw={} targetMask={} targetSf={} mac={} bursts={} ess={} enc={} contradictsClear={}",
                             candidateTarget, candidateTarget - targetFreqHz, candidateLive.stats.demodPath,
                             candidateScore, candidateStrong, candidateIsSeed, candidateIsNominal,
                             candidateFollowedSlotTelemetry,
                             coldAcquireUsefulTelemetry,
                             candidateEv.targetVoiceCodewords,
                             candidateEv.targetMaskedBursts, candidateEv.targetSuperframeBursts,
                             candidateLive.stats.phase2MacCrcValid,
                             candidateLive.stats.phase2Bursts,
                             candidateLive.stats.phase2EssKnown,
                             candidateLive.stats.phase2EssEncrypted,
                             candidateContradictsTrustedClear);
            }
            if (!candidateStrong && !candidateIsNominal && !unverifiedSeedHasUsefulTelemetry &&
                !coldAcquireUsefulTelemetry) {
                continue;
            }
            if ((candidateStrong && !bestStrong) ||
                (coldAcquireUsefulTelemetry && !bestStrong && candidateScore > bestScore) ||
                (unverifiedSeedHasUsefulTelemetry && !bestStrong && candidateScore > bestScore + 20) ||
                candidateScore > bestScore + 40 ||
                (candidateScore > bestScore &&
                 (candidateLive.stats.phase2MacCrcValid > live.stats.phase2MacCrcValid ||
                   candidateLive.stats.phase2EssKnown ||
                  candidateLive.stats.phase2MacPdus > live.stats.phase2MacPdus ||
                  candidateLive.stats.phase2Bursts > live.stats.phase2Bursts))) {
                bestScore = candidateScore;
                bestDecoder = std::move(candidateDecoder);
                live = std::move(candidateLive);
                effectiveTargetFreqHz = candidateTarget;
                const bool strongTargetVoiceEye =
                    candidateStrong &&
                    !candidateContradictsTrustedClear &&
                    live.stats.phase2SuperframeBursts >= 6 &&
                    live.stats.phase2MaskedBursts >= 6 &&
                    (live.stats.phase2VoiceCodewords >= 16 ||
                     live.stats.phase2MacCrcValid > 0 ||
                     live.stats.phase2EssKnown);
                if (strongTargetVoiceEye) break;
            }
        }
        if (std::abs(effectiveTargetFreqHz - initialEffectiveTargetFreqHz) >= 50.0) {
            if (logOffsetProbe) {
                spdlog::info("P25 P2 traffic offset probe selected: nominal={} effective={} off={} score={} bursts={} mac={}/{} vcw={}",
                             targetFreqHz, effectiveTargetFreqHz, effectiveTargetFreqHz - targetFreqHz,
                             bestScore, live.stats.phase2Bursts, live.stats.phase2MacCrcValid,
                             live.stats.phase2MacPdus, live.stats.phase2VoiceCodewords);
                // Probe copies intentionally use a cheap CQPSK budget; restore the
                // live traffic search ceiling before sticky lock continues.
                bestDecoder.setMaxCqpskSearchCandidates(
                    rx.p25VoiceLiveDecoder.config().maxCqpskSearchCandidates);
                bestDecoder.setRealtimeDecodeBudgetMs(
                    rx.p25VoiceLiveDecoder.config().realtimeDecodeBudgetMs);
            }
            rx.p25VoiceLiveDecoder = std::move(bestDecoder);
        }
    }
    if (deferOffsetProbeForSoftAfcPass &&
        live.stats.phase2Bursts == 0 &&
        live.stats.phase2VoiceCodewords == 0) {
        // Soft-AFC pass produced no eye — allow the next job to run offset probe.
        rx.p25Phase2TrafficTargetOffsetMisses =
            std::max(rx.p25Phase2TrafficTargetOffsetMisses, 1);
    }
    p25Phase2UpdateTrafficTargetOffsetLock(rx, live, targetFreqHz, effectiveTargetFreqHz);
    out.effectiveTargetFreqHz = effectiveTargetFreqHz;

    populateP25VoiceAudioBlockFromLive(out, live);
    if (!rx.p25VoiceTdmaSlotKnown) {
        out.phase2TargetMaskedBursts = out.phase2MaskedBursts;
        out.phase2TargetVoiceCodewords = out.phase2VoiceCodewords;
    }

    // Use configured Phase-2 symbol rate for abs-dibit mapping (always 6000),
    // not live.stats.symbolRate which can jitter and desync dedupe vs
    // alignPhase2AbsoluteDibitCursor (same formula uses config.symbolRate).
    const double absMapSymbolRate =
        (rx.p25VoiceLiveDecoder.config().symbolRate > 0.0)
            ? rx.p25VoiceLiveDecoder.config().symbolRate
            : ((std::isfinite(live.stats.symbolRate) && live.stats.symbolRate > 0.0)
                   ? live.stats.symbolRate
                   : 6000.0);
    const bool haveAbsoluteDibits = iqStartAbsoluteKnown &&
        std::isfinite(sampleRateHz) && sampleRateHz > 0.0 &&
        absMapSymbolRate > 0.0;
    const uint64_t windowStartAbsDibit = haveAbsoluteDibits
        ? static_cast<uint64_t>(std::llround(static_cast<long double>(iqStartAbsolute) *
                                             static_cast<long double>(absMapSymbolRate) /
                                             static_cast<long double>(sampleRateHz)))
        : 0;
    const size_t clampedContextIqSamples = std::min(contextIqSamples, iq.size());
    const bool haveFreshStartDibits = haveAbsoluteDibits && clampedContextIqSamples > 0;
    const uint64_t freshStartAbsDibit = haveFreshStartDibits
        ? windowStartAbsDibit + static_cast<uint64_t>(
              std::llround(static_cast<long double>(clampedContextIqSamples) *
                           static_cast<long double>(absMapSymbolRate) /
                           static_cast<long double>(sampleRateHz)))
        : 0;
    if (rx.p25VoicePhase2) {
        p25UpdateTrafficProcessorFromLiveDecode(rx, live, windowStartAbsDibit, haveAbsoluteDibits);
    }

    // Do not promote aggregate Phase-2 ESS/MAC stats into receiver-wide clear or
    // encrypted state here.  A single RF carrier carries both TDMA timeslots, and
    // live.stats can describe the opposite slot.  Target-slot-only security is
    // derived inside decodeP25Phase2VoiceBlock() and enforced by
    // applyP25Phase2SecurityAudioGate().
    if (!out.backendAvailable) {
        out.diag = chooseP25VoiceDiag(out);
        if (rx.p25VoicePhase2) writeP25Phase2ValidationRecord(rx, live, out, {}, sampleRateHz, centerFreqHz, effectiveTargetFreqHz, outputRateHz);
        return out;
    }

    if (rx.p25VoicePhase2) {
        auto phase2Out = decodeP25Phase2VoiceBlock(rx, live, out, sampleRateHz, centerFreqHz, effectiveTargetFreqHz,
            outputRateHz, windowStartAbsDibit, haveAbsoluteDibits, freshStartAbsDibit, haveFreshStartDibits);
        p25Phase2FinalizeAudioTailState(rx, phase2Out);
        const bool tailGrace = phase2Out.phase2AudioTailGraceActive;
        const bool staleTail = phase2Out.phase2StaleAudioTail;
        auto gatedOut = applyP25Phase2SecurityAudioGate(rx, std::move(phase2Out), effectiveTargetFreqHz);
        gatedOut.phase2AudioTailGraceActive = tailGrace;
        gatedOut.phase2StaleAudioTail = staleTail;
        // No opposite-slot / empty-hop PLC (SDRTrunk per-timeslot silence).
        p25Phase2AppendOppositeSlotSustainPlc(rx, gatedOut, outputRateHz);
        gatedOut.phase2SpeakerGateReason = p25VoiceBlockSpeakerGateReason(gatedOut);
        writeP25Phase2ValidationRecord(rx, live, gatedOut, {}, sampleRateHz, centerFreqHz, effectiveTargetFreqHz, outputRateHz);
        return gatedOut;
    }

    if (out.phase2VoiceCodewords > 0) {
        out.phase2VoiceUnsupported = true;
        out.diag = chooseP25VoiceDiag(out);
        writeP25Phase2ValidationRecord(rx, live, out, {}, sampleRateHz, centerFreqHz, effectiveTargetFreqHz, outputRateHz);
        return out;
    }

    return decodeP25Phase1VoiceBlock(rx, live, out, outputRateHz);
}


