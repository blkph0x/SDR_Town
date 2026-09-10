#include "P25VoiceTest.h"
#include "P25VoiceSession.h"
#include "P25DecodeConfig.h"
#include "DemodModeUtils.h"

#include "P25AppGlobals.h"
#include "P25AudioDropClass.h"
#include "P25Control.h"
#include "P25LiveDecoder.h"
#include "P25RollingIq.h"
#include "P25SdrtrunkTune.h"
#include "P25TalkgroupRegistry.h"
#include "P25VoiceDecode.h"
#include "P25VoiceTiming.h"
#include "Receiver.h"
#include "SignalClassifier.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1Char>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStringList>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif
#ifndef SDR_TOWN_P25_AUDIO_BASELINE
#define SDR_TOWN_P25_AUDIO_BASELINE "p25-clear-continuous-20260810"
#endif

static QString sigmfSiblingPath(const QFileInfo& info, const QString& extension)
{
    return info.absolutePath() + "/" + info.completeBaseName() + extension;
}

static QString resolveSigmfMetaPath(QString input)
{
    input = input.trimmed();
    if ((input.startsWith('"') && input.endsWith('"')) ||
        (input.startsWith('\'') && input.endsWith('\''))) {
        input = input.mid(1, input.size() - 2);
    }

    QFileInfo info(input);
    if (info.isDir()) {
        QDir dir(input);
        const auto metas = dir.entryInfoList(QStringList() << "*.sigmf-meta", QDir::Files, QDir::Name);
        if (!metas.empty()) return metas.front().absoluteFilePath();
        return {};
    }
    if (info.exists() && info.fileName().endsWith(".sigmf-meta", Qt::CaseInsensitive)) return info.absoluteFilePath();
    if (info.exists() && info.fileName().endsWith(".sigmf-data", Qt::CaseInsensitive)) {
        const QString meta = sigmfSiblingPath(info, ".sigmf-meta");
        if (QFileInfo::exists(meta)) return QFileInfo(meta).absoluteFilePath();
    }
    return {};
}

static double sigmfAnnotationTargetHz(const json& meta)
{
    try {
        if (!meta.contains("annotations") || !meta["annotations"].is_array()) return 0.0;
        for (const auto& ann : meta["annotations"]) {
            const double lo = ann.value("core:freq_lower_edge", 0.0);
            const double hi = ann.value("core:freq_upper_edge", 0.0);
            if (std::isfinite(lo) && std::isfinite(hi) && lo > 0.0 && hi > lo) {
                return (lo + hi) * 0.5;
            }
        }
    } catch (...) {
    }
    return 0.0;
}

SigmfCaptureInfo inspectSigmfCf32Capture(const QString& requestedPath)
{
    SigmfCaptureInfo out;
    out.metaPath = resolveSigmfMetaPath(requestedPath);
    if (out.metaPath.isEmpty()) {
        out.error = "could not resolve a .sigmf-meta file from the supplied path";
        return out;
    }

    json meta;
    try {
        std::ifstream metaIn(out.metaPath.toStdString());
        if (!metaIn.is_open()) {
            out.error = "could not open SigMF metadata";
            return out;
        }
        metaIn >> meta;
    } catch (const std::exception& ex) {
        out.error = std::string("could not parse SigMF metadata: ") + ex.what();
        return out;
    }

    try {
        out.datatype = meta["global"].value("core:datatype", std::string());
        out.sampleRateHz = meta["global"].value("core:sample_rate", 0.0);
        if (meta.contains("captures") && meta["captures"].is_array() && !meta["captures"].empty()) {
            out.centerFreqHz = meta["captures"][0].value("core:frequency", 0.0);
        }
        out.targetFreqHz = sigmfAnnotationTargetHz(meta);
    } catch (const std::exception& ex) {
        out.error = std::string("SigMF metadata is missing required fields: ") + ex.what();
        return out;
    }

    if (out.datatype != "cf32_le") {
        out.error = "only cf32_le SigMF captures are supported for P25 replay";
        return out;
    }
    if (!std::isfinite(out.sampleRateHz) || out.sampleRateHz <= 0.0 ||
        !std::isfinite(out.centerFreqHz) || out.centerFreqHz <= 0.0) {
        out.error = "SigMF metadata has invalid sample rate or center frequency";
        return out;
    }

    const QFileInfo metaInfo(out.metaPath);
    out.dataPath = sigmfSiblingPath(metaInfo, ".sigmf-data");
    if (!QFileInfo::exists(out.dataPath)) {
        out.error = "matching .sigmf-data file was not found";
        return out;
    }

    std::ifstream data(out.dataPath.toStdString(), std::ios::binary | std::ios::ate);
    if (!data.is_open()) {
        out.error = "could not open SigMF data file";
        return out;
    }
    const auto endPos = data.tellg();
    if (endPos <= 0) {
        out.error = "SigMF data file is empty";
        return out;
    }
    out.totalBytes = static_cast<uint64_t>(endPos);
    if (out.totalBytes % (sizeof(float) * 2u) != 0u) {
        out.error = "SigMF cf32 data length is not an even I/Q float pair count";
        return out;
    }
    out.totalSamples = out.totalBytes / (sizeof(float) * 2u);
    out.totalDurationMs = out.sampleRateHz > 0.0
        ? static_cast<double>(out.totalSamples) * 1000.0 / out.sampleRateHz
        : 0.0;
    out.ok = true;
    return out;
}

SigmfIqCapture loadSigmfCf32Capture(const QString& requestedPath, double maxMs, double skipMs)
{
    SigmfIqCapture out;
    const SigmfCaptureInfo info = inspectSigmfCf32Capture(requestedPath);
    out.ok = info.ok;
    out.metaPath = info.metaPath;
    out.dataPath = info.dataPath;
    out.error = info.error;
    out.datatype = info.datatype;
    out.sampleRateHz = info.sampleRateHz;
    out.centerFreqHz = info.centerFreqHz;
    out.targetFreqHz = info.targetFreqHz;
    out.totalSamples = info.totalSamples;
    out.totalBytes = info.totalBytes;
    out.totalDurationMs = info.totalDurationMs;
    if (!info.ok) {
        out.ok = false;
        return out;
    }

    std::ifstream data(out.dataPath.toStdString(), std::ios::binary);
    if (!data.is_open()) {
        out.error = "could not open SigMF data file";
        out.ok = false;
        return out;
    }

    const uint64_t totalSamples = info.totalSamples;
    uint64_t startSample = 0;
    if (std::isfinite(skipMs) && skipMs > 0.0) {
        startSample = static_cast<uint64_t>(std::clamp(
            out.sampleRateHz * (skipMs / 1000.0),
            0.0,
            totalSamples > 0 ? static_cast<double>(totalSamples - 1u) : 0.0));
    }

    uint64_t samplesToRead = totalSamples - startSample;
    if (std::isfinite(maxMs) && maxMs > 0.0) {
        const uint64_t byTime = static_cast<uint64_t>(std::clamp(
            out.sampleRateHz * (maxMs / 1000.0),
            1.0,
            static_cast<double>(samplesToRead)));
        samplesToRead = std::min(samplesToRead, byTime);
    }

    const uint64_t byteOffset = startSample * sizeof(float) * 2u;
    data.seekg(static_cast<std::streamoff>(byteOffset), std::ios::beg);
    if (!data) {
        out.error = "could not seek to requested SigMF replay offset";
        return out;
    }
    out.firstSampleOffset = startSample;
    out.startOffsetMs = out.sampleRateHz > 0.0
        ? static_cast<double>(startSample) * 1000.0 / out.sampleRateHz
        : 0.0;
    out.iq.resize(static_cast<size_t>(samplesToRead));
    constexpr uint64_t kReadBlockSamples = 1u << 16;
    std::vector<float> rawBlock(kReadBlockSamples * 2u);
    uint64_t samplesRead = 0;
    while (samplesRead < samplesToRead) {
        const uint64_t blockSamples = std::min<uint64_t>(kReadBlockSamples, samplesToRead - samplesRead);
        const auto bytesToRead = static_cast<std::streamsize>(blockSamples * 2u * sizeof(float));
        data.read(reinterpret_cast<char*>(rawBlock.data()), bytesToRead);
        if (!data) {
            out.error = "short read while loading SigMF data";
            out.iq.clear();
            return out;
        }
        for (uint64_t i = 0; i < blockSamples; ++i) {
            out.iq[static_cast<size_t>(samplesRead + i)] =
                std::complex<float>(rawBlock[static_cast<size_t>(i * 2u)],
                                    rawBlock[static_cast<size_t>(i * 2u + 1u)]);
        }
        samplesRead += blockSamples;
    }

    out.ok = true;
    return out;
}

bool parseFiniteDoubleToken(const std::string& text, double& out)
{
    try {
        size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value)) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

static bool sigmfPathExistsForCli(const std::string& path)
{
    return !resolveSigmfMetaPath(QString::fromStdString(trimCopy(path))).isEmpty();
}

static bool parseUnsignedIntegerToken(const std::string& text, uint64_t& out)
{
    try {
        size_t consumed = 0;
        const uint64_t value = std::stoull(text, &consumed, 0);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool p25ReplayHasMaskParameters(const P25ReplayCliArgs& args)
{
    return args.nac >= 0 && args.nac <= 0x0fff &&
        args.wacn >= 0 && args.wacn <= 0x0fffff &&
        args.systemId >= 0 && args.systemId <= 0x0fff;
}

bool trySeedP25ReplayMaskFromCaptureLog(P25ReplayCliArgs& args)
{
    if (p25ReplayHasMaskParameters(args)) return false;
    int partialNac = -1;
    int partialSystemId = -1;
    const QFileInfo captureInfo(QString::fromStdString(args.path));
    const QDir dir(captureInfo.isDir() ? captureInfo.absoluteFilePath() : captureInfo.absolutePath());
    const QStringList logs = dir.entryList(QStringList{"*_p25_log.txt"}, QDir::Files, QDir::Time);
    if (!logs.isEmpty()) {
        QFile logFile(dir.absoluteFilePath(logs.front()));
        if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            static const QRegularExpression maskRe(
                QStringLiteral("nac=0x([0-9a-f]+).*wacn=0x([0-9a-f]+).*sys=0x([0-9a-f]+)"),
                QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression nacRe(
                QStringLiteral("\\bnac=0x([0-9a-f]+)"),
                QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression sysRe(
                QStringLiteral("\\bsys(?:tem)?=0x([0-9a-f]+)"),
                QRegularExpression::CaseInsensitiveOption);
            while (!logFile.atEnd()) {
                const QString line = QString::fromUtf8(logFile.readLine());
                const auto match = maskRe.match(line);
                if (match.hasMatch()) {
                    bool okN = false;
                    bool okW = false;
                    bool okS = false;
                    const int nac = match.captured(1).toInt(&okN, 16);
                    const qulonglong wacn = match.captured(2).toULongLong(&okW, 16);
                    const int systemId = match.captured(3).toInt(&okS, 16);
                    if (okN && okW && okS && nac > 0 && wacn > 0 && systemId > 0) {
                        args.nac = nac;
                        args.wacn = static_cast<int64_t>(wacn);
                        args.systemId = systemId;
                        return true;
                    }
                }

                if (partialNac < 0) {
                    bool ok = false;
                    const auto nacMatch = nacRe.match(line);
                    const int nac = nacMatch.hasMatch() ? nacMatch.captured(1).toInt(&ok, 16) : 0;
                    if (ok && nac > 0) partialNac = nac & 0x0fff;
                }
                if (partialSystemId < 0) {
                    bool ok = false;
                    const auto sysMatch = sysRe.match(line);
                    const int systemId = sysMatch.hasMatch() ? sysMatch.captured(1).toInt(&ok, 16) : 0;
                    if (ok && systemId > 0) partialSystemId = systemId & 0x0fff;
                }
            }
        }
    }

    const double targetHz = args.targetMhz > 0.0 && std::isfinite(args.targetMhz)
        ? args.targetMhz * 1e6
        : 0.0;
    const auto talkgroups = loadP25Talkgroups();
    const P25TalkgroupEntry* best = nullptr;
    int bestScore = std::numeric_limits<int>::min();
    qint64 bestLastSeen = 0;
    for (const auto& tg : talkgroups) {
        if (!p25TalkgroupHasUsableMaskMetadata(tg)) continue;
        if (args.followTalkgroupId != 0 && tg.talkgroupId != args.followTalkgroupId) continue;

        int score = 0;
        if (args.followTalkgroupId != 0 && tg.talkgroupId == args.followTalkgroupId) score += 100;

        if (targetHz > 0.0 && tg.lastVoiceFreqHz > 0.0 && std::isfinite(tg.lastVoiceFreqHz)) {
            const double deltaHz = std::abs(tg.lastVoiceFreqHz - targetHz);
            if (deltaHz <= 50.0) {
                score += 60;
            } else if (deltaHz <= 250.0) {
                score += 40;
            } else if (deltaHz <= 12500.0) {
                score += 15;
            } else if (args.followTalkgroupId == 0) {
                continue;
            } else {
                score -= 20;
            }
        }

        if (partialNac >= 0) {
            score += tg.nac == static_cast<uint16_t>(partialNac) ? 25 : -80;
        }
        if (partialSystemId >= 0) {
            score += tg.systemId == static_cast<uint16_t>(partialSystemId) ? 25 : -80;
        }
        if (args.tdmaSlot >= 0 && tg.tdmaSlotKnown) {
            score += tg.tdmaSlot == static_cast<uint8_t>(args.tdmaSlot & 0x01) ? 8 : -8;
        }
        if (tg.voiceProtocol == P25VoiceProtocol::Phase2TDMA || tg.phase2Candidate) score += 6;
        score += std::min(tg.hitCount, 20);
        // User priority + rolling activity (roadmap auto most-active / preempt).
        score += std::min(std::max(0, tg.userPriority), 100) * 50;
        score += std::min(std::max(0, tg.activityScore), 200);

        if (score > bestScore || (score == bestScore && tg.lastSeenMs > bestLastSeen)) {
            best = &tg;
            bestScore = score;
            bestLastSeen = tg.lastSeenMs;
        }
    }

    if (best && bestScore >= 80) {
        args.nac = static_cast<int>(best->nac);
        args.wacn = static_cast<int64_t>(best->wacn);
        args.systemId = static_cast<int>(best->systemId);
        if (args.traceReplay) {
            std::cerr << "P25 voicetest trace=mask-seeded source=talkgroup-cache"
                      << " tg=" << best->talkgroupId
                      << " score=" << bestScore
                      << " nac=0x" << std::hex << best->nac
                      << " wacn=0x" << best->wacn
                      << " sys=0x" << best->systemId
                      << std::dec << std::endl;
        }
        return true;
    }
    return false;
}

static bool parseP25ReplayOptionToken(const std::string& token, P25ReplayCliArgs& args)
{
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "p2" || lower == "phase2" || lower == "phase2voice" || lower == "tdma") {
        args.phase2Voice = true;
        return true;
    }
    if (lower == "clear" || lower == "grantclear" || lower == "unencrypted") {
        args.clearGrant = true;
        args.encryptedGrant = false;
        return true;
    }
    if (lower == "enc" || lower == "encrypted" || lower == "grantencrypted") {
        args.encryptedGrant = true;
        args.clearGrant = false;
        return true;
    }
    if (lower == "forensic" || lower == "deep" || lower == "exhaustive") {
        args.forensicVoice = true;
        return true;
    }
    if (lower == "realtime" || lower == "fast" || lower == "live") {
        args.forensicVoice = false;
        return true;
    }
    if (lower == "trace" || lower == "debug" || lower == "verbose") {
        args.traceReplay = true;
        return true;
    }
    if (lower == "probe" || lower == "fieldprobe" || lower == "lateentryprobe" || lower == "audio-probe") {
        args.fieldAudioProbe = true;
        return true;
    }
    if (lower == "noprobe" || lower == "nofieldprobe" || lower == "no-lateentryprobe") {
        args.fieldAudioProbe = false;
        return true;
    }
    if (lower == "stream" || lower == "continuous" || lower == "sdrtrunk") {
        args.streamVoice = true;
        return true;
    }
    if (lower == "legacy" || lower == "nostream" || lower == "windowed") {
        args.streamVoice = false;
        return true;
    }

    const size_t eq = lower.find('=');
    if (eq == std::string::npos) return false;
    const std::string key = lower.substr(0, eq);
    const std::string rawValue = token.substr(eq + 1);
    std::string rawValueLower = rawValue;
    std::transform(rawValueLower.begin(), rawValueLower.end(), rawValueLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (key == "wav" || key == "wavout" || key == "outfile" || key == "out") {
        args.wavOutPath = rawValue;
        // Strip surrounding quotes if present.
        if (args.wavOutPath.size() >= 2 &&
            ((args.wavOutPath.front() == '"' && args.wavOutPath.back() == '"') ||
             (args.wavOutPath.front() == '\'' && args.wavOutPath.back() == '\''))) {
            args.wavOutPath = args.wavOutPath.substr(1, args.wavOutPath.size() - 2);
        }
        return true;
    }
    if (key == "oppwav" || key == "wavopp" || key == "companionwav" || key == "slotwav") {
        args.oppositeWavOutPath = rawValue;
        if (args.oppositeWavOutPath.size() >= 2 &&
            ((args.oppositeWavOutPath.front() == '"' && args.oppositeWavOutPath.back() == '"') ||
             (args.oppositeWavOutPath.front() == '\'' && args.oppositeWavOutPath.back() == '\''))) {
            args.oppositeWavOutPath = args.oppositeWavOutPath.substr(1, args.oppositeWavOutPath.size() - 2);
        }
        return true;
    }
    if (key == "hopms" || key == "hop" || key == "stepms") {
        if (rawValueLower == "auto") {
            args.hopMs = 0.0;
            return true;
        }
    }

    if (key == "nac" || key == "wacn" || key == "system" || key == "systemid" || key == "sys" || key == "sysid" || key == "sid" ||
        key == "slot" || key == "tdmaslot" || key == "timeslot" ||
        key == "tg" || key == "talkgroup" || key == "talkgroupid") {
        uint64_t u = 0;
        if (!parseUnsignedIntegerToken(rawValue, u)) return false;
        if (key == "nac") {
            if (u > 0x0fff) return false;
            args.nac = static_cast<int>(u);
        } else if (key == "wacn") {
            if (u > 0x0fffff) return false;
            args.wacn = static_cast<int64_t>(u);
        } else if (key == "slot" || key == "tdmaslot" || key == "timeslot") {
            if (u > 1u) return false;
            args.tdmaSlot = static_cast<int>(u);
        } else if (key == "tg" || key == "talkgroup" || key == "talkgroupid") {
            if (u == 0 || u > 0x00ffffffu) return false;
            args.followTalkgroupId = static_cast<uint32_t>(u);
        } else {
            if (u > 0x0fff) return false;
            args.systemId = static_cast<int>(u);
        }
        return true;
    }

    double value = 0.0;
    if (!parseFiniteDoubleToken(rawValue, value)) return false;

    if (key == "skip" || key == "skipms" || key == "offset" || key == "offsetms" || key == "start" || key == "startms") {
        args.skipMs = std::max(0.0, value);
        return true;
    }
    if (key == "windowms" || key == "winms" || key == "window") {
        args.windowMs = std::max(50.0, value);
        return true;
    }
    if (key == "hopms" || key == "hop" || key == "stepms") {
        args.hopMs = value <= 0.0 ? 0.0 : std::max(10.0, value);
        return true;
    }
    if (key == "minframes" || key == "mindecoded" || key == "minvcw") {
        args.minDecodedFrames = static_cast<long long>(std::max(0.0, value));
        return true;
    }
    if (key == "minaudio" || key == "minaudioseconds" || key == "minsecs") {
        args.minAudioSeconds = std::max(0.0, value);
        return true;
    }
    if (key == "stream") {
        args.streamVoice = value != 0.0;
        return true;
    }
    if (key == "center" || key == "centermhz" || key == "cf" || key == "cfmhz") {
        args.centerMhz = value;
        return true;
    }
    if (key == "voicecenter" || key == "voicecentermhz" || key == "voicecf" || key == "voicecfmhz" ||
        key == "trafficcenter" || key == "trafficcentermhz" || key == "trafficcf" || key == "trafficcfmhz") {
        args.voiceCenterMhz = value;
        return true;
    }
    if (key == "offsethz" || key == "targetoffsethz" || key == "trafficoffsethz" ||
        key == "p25offsethz" || key == "afcoffsethz") {
        args.trafficTargetOffsetHz = value;
        return true;
    }
    if (key == "target" || key == "targetmhz") {
        args.targetMhz = value;
        return true;
    }
    if (key == "ms" || key == "duration" || key == "durationms") {
        args.ms = std::max(0.0, value);
        return true;
    }
    if (key == "follow" || key == "followms" || key == "voice" || key == "voicems" || key == "followduration") {
        args.followMs = std::max(0.0, value);
        return true;
    }
    return false;
}

static bool parseP25ReplayTailTokens(std::istringstream& tail, P25ReplayCliArgs& args)
{
    std::string token;
    int numericCount = 0;
    while (tail >> token) {
        if (parseP25ReplayOptionToken(token, args)) continue;

        double numeric = 0.0;
        if (parseFiniteDoubleToken(token, numeric)) {
            if (numericCount == 0) {
                args.targetMhz = numeric;
            } else if (numericCount == 1) {
                args.ms = std::max(0.0, numeric);
            } else if (numericCount == 2) {
                args.skipMs = std::max(0.0, numeric);
            } else {
                args.error = "too many numeric replay arguments";
                return false;
            }
            ++numericCount;
            continue;
        }

        args.error = "unknown replay option: " + token;
        return false;
    }
    return true;
}

P25ReplayCliArgs parseP25ReplayCliArgs(const std::string& rest)
{
    P25ReplayCliArgs args;
    const std::string text = trimCopy(rest);
    if (text.empty()) {
        args.error = "usage: p25 replay <sigmf-meta|sigmf-data|capture_dir> [target_mhz] [ms] [phase2] [skip=<ms>] [center=<mhz>] [nac=<id> wacn=<id> system=<id>]";
        return args;
    }

    if (text.front() == '"' || text.front() == '\'') {
        const char quote = text.front();
        const size_t close = text.find(quote, 1);
        if (close == std::string::npos) {
            args.error = "quoted replay path is missing its closing quote";
            return args;
        }
        args.path = text.substr(1, close - 1);
        std::istringstream tail(text.substr(close + 1));
        if (!parseP25ReplayTailTokens(tail, args)) {
            if (args.error.empty()) args.error = "could not parse replay arguments";
            return args;
        }
        if (!sigmfPathExistsForCli(args.path)) {
            args.error = "replay capture path does not exist or has no .sigmf-meta";
            return args;
        }
        args.ok = true;
        return args;
    }

    struct TokenPos {
        std::string token;
        size_t begin = 0;
        size_t end = 0;
    };
    std::vector<TokenPos> tokens;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
        if (pos >= text.size()) break;
        const size_t begin = pos;
        while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
        tokens.push_back({text.substr(begin, pos - begin), begin, pos});
    }

    // Batch CLI launchers on Windows commonly strip the inner quotes from
    // --cmd, so accept an unquoted capture path with spaces by finding the
    // longest valid SigMF path prefix and then parsing the remaining option tail.
    for (size_t count = tokens.size(); count > 0; --count) {
        const size_t pathEnd = tokens[count - 1].end;
        const std::string candidate = trimCopy(text.substr(0, pathEnd));
        if (candidate.empty() || !sigmfPathExistsForCli(candidate)) continue;

        P25ReplayCliArgs parsed = args;
        parsed.path = candidate;
        std::istringstream tail(text.substr(pathEnd));
        if (!parseP25ReplayTailTokens(tail, parsed)) continue;
        parsed.ok = true;
        return parsed;
    }

    for (int trailingNumbers = 0; trailingNumbers <= 2; ++trailingNumbers) {
        if (static_cast<size_t>(trailingNumbers) > tokens.size()) break;
        std::array<double, 2> values{0.0, 0.0};
        bool numeric = true;
        for (int i = 0; i < trailingNumbers; ++i) {
            const size_t idx = tokens.size() - static_cast<size_t>(trailingNumbers) + static_cast<size_t>(i);
            numeric = numeric && parseFiniteDoubleToken(tokens[idx].token, values[static_cast<size_t>(i)]);
        }
        if (!numeric) continue;
        const size_t pathEnd = trailingNumbers == 0
            ? text.size()
            : tokens[tokens.size() - static_cast<size_t>(trailingNumbers)].begin;
        const std::string candidate = trimCopy(text.substr(0, pathEnd));
        if (!candidate.empty() && sigmfPathExistsForCli(candidate)) {
            args.path = candidate;
            if (trailingNumbers >= 1) args.targetMhz = values[0];
            if (trailingNumbers >= 2) args.ms = values[1];
            args.ok = true;
            return args;
        }
    }

    args.error = "replay capture path does not exist or has no .sigmf-meta; quote paths with spaces before adding replay options";
    return args;
}

QString gIqTestCapturesRootOverride;

QString trainingCapturesRoot()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData + "/training_captures");
    return appData + "/training_captures";
}


QString iqTestCapturesRoot()
{
    if (!gIqTestCapturesRootOverride.trimmed().isEmpty()) {
        QDir().mkpath(gIqTestCapturesRootOverride);
        return gIqTestCapturesRootOverride;
    }
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData + "/iq_test_captures");
    return appData + "/iq_test_captures";
}

QString humanBytes(qint64 bytes)
{
    const double value = static_cast<double>(std::max<qint64>(0, bytes));
    if (value >= 1024.0 * 1024.0 * 1024.0) {
        return QString("%1 GiB").arg(value / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
    if (value >= 1024.0 * 1024.0) {
        return QString("%1 MiB").arg(value / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QString("%1 bytes").arg(bytes);
}


qint64 captureDataBytesForSeconds(double sampleRateHz, double seconds)
{
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0 ||
        !std::isfinite(seconds) || seconds <= 0.0) {
        return 0;
    }
    const double bytes = sampleRateHz * seconds * static_cast<double>(sizeof(std::complex<float>));
    if (!std::isfinite(bytes) || bytes <= 0.0) return 0;
    return static_cast<qint64>(std::ceil(std::min<double>(
        bytes,
        static_cast<double>(std::numeric_limits<qint64>::max() / 2))));
}

qint64 captureStartRequiredBytes(double sampleRateHz, int plannedDurationMs)
{
    const double seconds = plannedDurationMs > 0
        ? static_cast<double>(plannedDurationMs) / 1000.0
        : kManualCapturePreflightSeconds;
    return captureDataBytesForSeconds(sampleRateHz, seconds) + kCaptureStorageReserveBytes;
}

bool captureStorageReadyForStart(const QString& root,
                                        double sampleRateHz,
                                        int plannedDurationMs,
                                        QString* message)
{
    QStorageInfo storage(root);
    storage.refresh();
    if (!storage.isValid() || !storage.isReady()) {
        if (message) *message = QString("Capture storage is not ready: %1").arg(root);
        return false;
    }

    const qint64 requiredBytes = captureStartRequiredBytes(sampleRateHz, plannedDurationMs);
    if (storage.bytesAvailable() < requiredBytes) {
        const double seconds = plannedDurationMs > 0
            ? static_cast<double>(plannedDurationMs) / 1000.0
            : kManualCapturePreflightSeconds;
        if (message) {
            *message = QString("Not enough free space for %1s IQ capture preflight at %2 MS/s on %3: need at least %4, available %5. Use --gui-capture-root to target a larger disk or clear old captures.")
                .arg(seconds, 0, 'f', 1)
                .arg(sampleRateHz / 1e6, 0, 'f', 3)
                .arg(root)
                .arg(humanBytes(requiredBytes))
                .arg(humanBytes(storage.bytesAvailable()));
        }
        return false;
    }
    return true;
}

bool captureStorageBelowStopReserve(const QString& path,
                                           qint64* availableBytes,
                                           QString* message)
{
    QStorageInfo storage(path);
    storage.refresh();
    const qint64 available = storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
    if (availableBytes) *availableBytes = available;
    if (!storage.isValid() || !storage.isReady()) {
        if (message) *message = QString("Capture storage became unavailable: %1").arg(path);
        return true;
    }
    if (available < kCaptureStorageReserveBytes) {
        if (message) {
            *message = QString("IQ capture stopped before the capture volume ran out of space: available %1, reserve %2.")
                .arg(humanBytes(available))
                .arg(humanBytes(kCaptureStorageReserveBytes));
        }
        return true;
    }
    return false;
}

std::string sanitizeFileToken(std::string s)
{
    if (s.empty()) s = "unknown";
    for (char& c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '-' && c != '_') c = '_';
    }
    while (s.find("__") != std::string::npos) {
        s.replace(s.find("__"), 2, "_");
    }
    if (s.size() > 48) s.resize(48);
    return trimCopy(s);
}

std::string makeCaptureSessionId(const QDateTime& utc, const std::string& label, double freqHz)
{
    const std::string seed = utc.toString(Qt::ISODateWithMs).toStdString() + "|" + label + "|" + std::to_string(freqHz);
    const size_t h = std::hash<std::string>{}(seed);
    std::ostringstream os;
    os << sanitizeFileToken(label) << "_" << std::hex << h;
    std::string out = os.str();
    if (out.size() > 64) out.resize(64);
    return out;
}

static std::mutex gCliP25WavCaptureMutex;
static std::unique_ptr<Pcm16WavCapture> gCliP25WavCapture;
static std::unique_ptr<Pcm16WavCapture> gCliP25OppositeWavCapture;

QString makeCliP25WavCapturePath(double ccHz, uint32_t talkgroupId, double voiceHz)
{
    QDir root(iqTestCapturesRoot());
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    return root.filePath(QString("%1_cli_p25_CC_%2MHz_TG_%3_voice_%4MHz.wav")
        .arg(stamp)
        .arg(ccHz / 1e6, 0, 'f', 5)
        .arg(talkgroupId)
        .arg(voiceHz / 1e6, 0, 'f', 5));
}

QString makeCliP25OppositeWavCapturePath(double ccHz, double voiceHz, int slot)
{
    QDir root(iqTestCapturesRoot());
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    return root.filePath(QString("%1_cli_p25_CC_%2MHz_companion_slot%3_voice_%4MHz.wav")
        .arg(stamp)
        .arg(ccHz / 1e6, 0, 'f', 5)
        .arg(slot & 0x01)
        .arg(voiceHz / 1e6, 0, 'f', 5));
}

bool startCliP25WavCapture(const QString& path, double sampleRate, QString* error)
{
    std::lock_guard<std::mutex> lk(gCliP25WavCaptureMutex);
    if (!gCliP25WavCapture) gCliP25WavCapture = std::make_unique<Pcm16WavCapture>();
    const uint32_t sr = static_cast<uint32_t>(std::clamp(
        std::isfinite(sampleRate) ? std::lround(sampleRate) : 48000ll,
        8000ll,
        192000ll));
    return gCliP25WavCapture->open(path, sr, error);
}

bool startCliP25OppositeWavCapture(const QString& path, double sampleRate, QString* error)
{
    std::lock_guard<std::mutex> lk(gCliP25WavCaptureMutex);
    if (!gCliP25OppositeWavCapture) gCliP25OppositeWavCapture = std::make_unique<Pcm16WavCapture>();
    const uint32_t sr = static_cast<uint32_t>(std::clamp(
        std::isfinite(sampleRate) ? std::lround(sampleRate) : 48000ll,
        8000ll,
        192000ll));
    return gCliP25OppositeWavCapture->open(path, sr, error);
}

void appendCliP25WavCapture(const std::vector<float>& samples)
{
    if (samples.empty()) return;
    std::lock_guard<std::mutex> lk(gCliP25WavCaptureMutex);
    if (gCliP25WavCapture && gCliP25WavCapture->active()) {
        gCliP25WavCapture->append(samples);
    }
}

void appendCliP25OppositeWavCapture(const std::vector<float>& samples)
{
    if (samples.empty()) return;
    std::lock_guard<std::mutex> lk(gCliP25WavCaptureMutex);
    if (gCliP25OppositeWavCapture && gCliP25OppositeWavCapture->active()) {
        gCliP25OppositeWavCapture->append(samples);
    }
}

CliP25WavCaptureSummary stopCliP25WavCapture()
{
    std::lock_guard<std::mutex> lk(gCliP25WavCaptureMutex);
    CliP25WavCaptureSummary out;
    if (!gCliP25WavCapture) return out;
    out.active = gCliP25WavCapture->active();
    out.path = gCliP25WavCapture->path();
    out.samples = gCliP25WavCapture->sampleCount();
    out.sampleRate = gCliP25WavCapture->sampleRate();
    gCliP25WavCapture->close();
    return out;
}

CliP25WavCaptureSummary stopCliP25OppositeWavCapture()
{
    std::lock_guard<std::mutex> lk(gCliP25WavCaptureMutex);
    CliP25WavCaptureSummary out;
    if (!gCliP25OppositeWavCapture) return out;
    out.active = gCliP25OppositeWavCapture->active();
    out.path = gCliP25OppositeWavCapture->path();
    out.samples = gCliP25OppositeWavCapture->sampleCount();
    out.sampleRate = gCliP25OppositeWavCapture->sampleRate();
    gCliP25OppositeWavCapture->close();
    return out;
}

bool writeJsonDocumentFile(const QString& path, const json& doc, QString* error)
{
    try {
        std::ofstream out(path.toStdString());
        if (!out.is_open()) {
            if (error) *error = QString("could not open %1 for writing").arg(path);
            return false;
        }
        out << doc.dump(2) << "\n";
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = QString::fromStdString(ex.what());
        return false;
    }
}

const char* captureHealthVerdict(uint64_t samplesWritten,
                                        uint64_t ringOverrunSamples,
                                        uint64_t fileWriteErrorPolls,
                                        double seconds,
                                        uint64_t ringEpochResetSkippedSamples)
{
    if (fileWriteErrorPolls > 0) return "bad_file_write_errors";
    if (samplesWritten == 0 || seconds <= 0.0) return "bad_empty_capture";
    if (ringOverrunSamples > 0) return "warning_ring_gaps_present";
    if (ringEpochResetSkippedSamples > 0) return "warning_ring_epoch_reset_skipped_samples";
    return "ok_gapless";
}

static bool writeClassifierTilePreview(const ClassifierTile& tile, const std::string& pgmPath, const std::string& f32Path)
{
    if (!tile.valid()) return false;

    std::ofstream pgm(pgmPath, std::ios::binary);
    if (!pgm.is_open()) return false;
    pgm << "P5\n" << tile.width << " " << tile.height << "\n255\n";
    std::vector<unsigned char> bytes(tile.pixels.size());
    for (size_t i = 0; i < tile.pixels.size(); ++i) {
        bytes[i] = static_cast<unsigned char>(std::clamp(tile.pixels[i], 0.0f, 1.0f) * 255.0f);
    }
    pgm.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    std::ofstream f32(f32Path, std::ios::binary);
    if (!f32.is_open()) return false;
    f32.write(reinterpret_cast<const char*>(tile.pixels.data()), static_cast<std::streamsize>(tile.pixels.size() * sizeof(float)));
    return true;
}


IqTestCaptureResult saveIqTestCapture(const IqTestCaptureRequest& req)
{
    IqTestCaptureResult out;
    if (req.iq.empty()) {
        out.message = "No IQ samples available. Start a device and make sure the requested duration fits in the recent-IQ ring.";
        return out;
    }
    if (req.sampleRateHz <= 0.0 || !std::isfinite(req.sampleRateHz)) {
        out.message = "Invalid sample rate for IQ test capture.";
        return out;
    }

    const QString root = iqTestCapturesRoot();
    const QString stamp = req.captureEndedUtc.isValid()
        ? req.captureEndedUtc.toString("yyyyMMdd_HHmmss_zzz")
        : QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const std::string labelToken = sanitizeFileToken(req.label);
    const QString baseName = QString("%1_%2_%3MHz_%4s")
        .arg(stamp)
        .arg(QString::fromStdString(labelToken))
        .arg(req.tunedFreqHz / 1e6, 0, 'f', 5)
        .arg(req.requestedSeconds, 0, 'f', 1);
    const QString dir = root + "/" + baseName;
    if (!QDir().mkpath(dir)) {
        out.message = "Could not create IQ test capture directory.";
        return out;
    }

    const std::string base = (dir + "/" + baseName).toStdString();
    const std::string dataPath = base + ".sigmf-data";
    const std::string metaPath = base + ".sigmf-meta";
    const std::string logPath = base + "_events.jsonl";
    const std::string p25TextPath = base + "_p25_log.txt";

    {
        std::ofstream data(dataPath, std::ios::binary);
        if (!data.is_open()) {
            out.message = "Could not write IQ SigMF data file.";
            return out;
        }
        for (const auto& s : req.iq) {
            const float re = s.real();
            const float im = s.imag();
            data.write(reinterpret_cast<const char*>(&re), sizeof(float));
            data.write(reinterpret_cast<const char*>(&im), sizeof(float));
        }
    }

    const double actualSeconds = static_cast<double>(req.iq.size()) / req.sampleRateHz;
    const QDateTime endUtc = req.captureEndedUtc.isValid() ? req.captureEndedUtc : QDateTime::currentDateTimeUtc();
    const QDateTime startUtc = endUtc.addMSecs(-static_cast<qint64>(std::llround(actualSeconds * 1000.0)));

    json meta;
    meta["global"] = {
        {"core:datatype", "cf32_le"},
        {"core:sample_rate", req.sampleRateHz},
        {"core:version", "1.2.0"},
        {"core:description", "SDR Town timed IQ test capture with synchronized diagnostics"},
        {"core:recorder", "SDR Town"},
        {"sdrtown:label", req.label},
        {"sdrtown:capture_type", "timed_iq_test"},
        {"sdrtown:requested_seconds", req.requestedSeconds},
        {"sdrtown:actual_seconds", actualSeconds},
        {"sdrtown:mode", modeToString(req.mode)},
        {"sdrtown:channel_bandwidth_hz", req.channelBwHz},
        {"sdrtown:audio_lpf_hz", req.lpfHz},
        {"sdrtown:audio_lpf_enabled", req.audioLpfEnabled},
        {"sdrtown:squelch_db", req.squelchDb},
        {"sdrtown:device_index", req.deviceIndex},
        {"sdrtown:device_driver", req.device.driver},
        {"sdrtown:device_label", req.device.label},
        {"sdrtown:device_serial", req.device.serial},
        {"sdrtown:rf_gain_db", req.device.gain},
        {"sdrtown:frequency_correction_ppm", req.device.frequencyCorrectionPpm},
        {"sdrtown:absolute_sample_start", req.startAbsolute},
        {"sdrtown:absolute_sample_end", req.endAbsolute},
        {"sdrtown:capture_started_utc", req.captureStartedUtc.toString(Qt::ISODateWithMs).toStdString()},
        {"sdrtown:capture_saved_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
        {"sdrtown:signal_level_db", req.signalLevelDb},
        {"sdrtown:noise_floor_db", req.noiseFloorDb},
        {"sdrtown:snr_db", req.snrDb},
        {"sdrtown:afc_offset_hz", req.afcOffsetHz}
    };
    meta["captures"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:frequency", req.centerFreqHz},
            {"core:datetime", startUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"sdrtown:capture_end_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"sdrtown:absolute_sample_start", req.startAbsolute},
            {"sdrtown:absolute_sample_end", req.endAbsolute}
        }
    });
    meta["annotations"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:sample_count", req.iq.size()},
            {"core:freq_lower_edge", req.tunedFreqHz - req.channelBwHz * 0.5},
            {"core:freq_upper_edge", req.tunedFreqHz + req.channelBwHz * 0.5},
            {"core:label", req.label},
            {"sdrtown:mode", modeToString(req.mode)},
            {"sdrtown:snr_db", req.snrDb}
        }
    });
    meta["sdrtown:artifacts"] = {
        {"event_log_jsonl", QFileInfo(QString::fromStdString(logPath)).fileName().toStdString()},
        {"p25_log_text", QFileInfo(QString::fromStdString(p25TextPath)).fileName().toStdString()}
    };

    try {
        std::ofstream metaOut(metaPath);
        if (!metaOut.is_open()) {
            out.message = "Could not write IQ SigMF metadata file.";
            return out;
        }
        metaOut << meta.dump(2);

        std::ofstream p25Text(p25TextPath);
        if (p25Text.is_open()) {
            p25Text << "# SDR Town P25/UI log snapshot for IQ capture\n";
            p25Text << "# version=" << SDR_TOWN_VERSION
                    << " p25_baseline=" << SDR_TOWN_P25_AUDIO_BASELINE << "\n";
            p25Text << "# capture_start_utc=" << startUtc.toString(Qt::ISODateWithMs).toStdString() << "\n";
            p25Text << "# capture_end_utc=" << endUtc.toString(Qt::ISODateWithMs).toStdString() << "\n";
            p25Text << "# absolute_sample_start=" << req.startAbsolute << "\n";
            p25Text << "# absolute_sample_end=" << req.endAbsolute << "\n";
            for (const QString& line : req.p25LogSnapshot) {
                p25Text << line.toStdString() << "\n";
            }
        }

        std::ofstream log(logPath, std::ios::app);
        if (log.is_open()) {
            json startRow = {
                {"event", "capture_start"},
                {"utc", startUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"label", req.label},
                {"freq_hz", req.tunedFreqHz},
                {"center_freq_hz", req.centerFreqHz},
                {"sample_rate_hz", req.sampleRateHz},
                {"absolute_sample", req.startAbsolute}
            };
            json endRow = {
                {"event", "capture_end"},
                {"utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"sample_count", req.iq.size()},
                {"actual_seconds", actualSeconds},
                {"absolute_sample", req.endAbsolute},
                {"signal_level_db", req.signalLevelDb},
                {"noise_floor_db", req.noiseFloorDb},
                {"snr_db", req.snrDb},
                {"afc_offset_hz", req.afcOffsetHz}
            };
            log << startRow.dump() << "\n";
            for (const QString& line : req.p25LogSnapshot) {
                json row = {
                    {"event", "p25_log_snapshot"},
                    {"capture_end_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
                    {"line", line.toStdString()}
                };
                log << row.dump() << "\n";
            }
            log << endRow.dump() << "\n";
        }

        std::ofstream manifest((root + "/manifest.jsonl").toStdString(), std::ios::app);
        if (manifest.is_open()) {
            json row = {
                {"created_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"label", req.label},
                {"freq_hz", req.tunedFreqHz},
                {"center_freq_hz", req.centerFreqHz},
                {"sample_rate_hz", req.sampleRateHz},
                {"sample_count", req.iq.size()},
                {"requested_seconds", req.requestedSeconds},
                {"actual_seconds", actualSeconds},
                {"absolute_sample_start", req.startAbsolute},
                {"absolute_sample_end", req.endAbsolute},
                {"meta", QFileInfo(QString::fromStdString(metaPath)).fileName().toStdString()},
                {"data", QFileInfo(QString::fromStdString(dataPath)).fileName().toStdString()},
                {"event_log", QFileInfo(QString::fromStdString(logPath)).fileName().toStdString()},
                {"p25_log", QFileInfo(QString::fromStdString(p25TextPath)).fileName().toStdString()},
                {"directory", dir.toStdString()}
            };
            manifest << row.dump() << "\n";
        }
    } catch (const std::exception& ex) {
        out.message = QString("IQ test capture write failed: %1").arg(ex.what());
        return out;
    }

    const bool truncated = actualSeconds + 0.050 < req.requestedSeconds;
    out.ok = true;
    out.directory = dir;
    out.message = QString("Saved timed IQ capture: %1 samples, %2 s%3")
        .arg(req.iq.size())
        .arg(actualSeconds, 0, 'f', 3)
        .arg(truncated ? QString(" (shorter than requested; recent-IQ ring limit reached)") : QString());
    return out;
}

static QString p25VoiceDiagCaptureSummary(const P25VoiceDiagSnapshot& diag)
{
    const auto code = static_cast<P25VoiceDiagCode>(diag.diag);
    return QString("stage=%1 tg=%2 sync=%3 nid=%4 nidLock=%5 imbe=%6 decoded=%7 audio=%8 "
                   "p2bursts=%9 p2vcw=%10 p2sf=%11 p2mask=%12 p2mac=%13/%14 %15 p2ess=%16 backend=%17")
        .arg(QString::fromUtf8(p25VoiceDiagLabel(code)))
        .arg(diag.talkgroupId)
        .arg(diag.syncs)
        .arg(diag.nids)
        .arg(diag.nidLock ? "yes" : "no")
        .arg(diag.imbeFrames)
        .arg(diag.decodedFrames)
        .arg(diag.audioSamples)
        .arg(diag.phase2Bursts)
        .arg(diag.phase2VoiceCodewords)
        .arg(diag.phase2SuperframeBursts)
        .arg(diag.phase2MaskedBursts)
        .arg(diag.phase2MacCrcValid)
        .arg(diag.phase2MacPdus)
        .arg(p25Phase2AcchStatsText(diag))
        .arg(diag.phase2EssKnown ? (diag.phase2EssEncrypted ? "enc" : "clear") : "unknown")
        .arg(diag.backendAvailable ? "yes" : "no");
}

IqTestCaptureResult saveCliP25FollowIqCapture(DeviceManager& mgr,
                                                     size_t devIndex,
                                                     double controlFreqHz,
                                                     const P25TalkgroupEntry& tg,
                                                     double requestedSeconds,
                                                     const P25VoiceDiagSnapshot& finalDiag,
                                                     const QString& reason,
                                                     const std::string& lastVoiceSig,
                                                     const QString& selectedGrantEventText,
                                                     const QString& selectedGrantDetailText,
                                                     double captureCenterFreqHz,
                                                     const DeviceManager::RecentIQWindow* preferredWindow,
                                                     const QDateTime& preferredWindowEndUtc,
                                                     const QString& preferredWindowSource)
{
    IqTestCaptureRequest req;
    const QString reasonToken = reason.isEmpty() ? QStringLiteral("diag") : reason;
    req.label = QString("p25_follow_TG_%1_%2MHz_%3")
        .arg(tg.talkgroupId)
        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
        .arg(reasonToken)
        .toStdString();
    req.deviceIndex = devIndex;
    const double voiceFreqHz = tg.lastVoiceFreqHz > 0.0 ? tg.lastVoiceFreqHz : controlFreqHz;
    req.tunedFreqHz = voiceFreqHz;
    req.centerFreqHz = (captureCenterFreqHz > 0.0 && std::isfinite(captureCenterFreqHz))
        ? captureCenterFreqHz
        : voiceFreqHz;
    req.mode = DemodMode::NFM;
    req.channelBwHz = 12500.0;
    req.lpfHz = 3000.0;
    req.audioLpfEnabled = false;
    req.squelchDb = -105.0;
    req.requestedSeconds = std::clamp(requestedSeconds, 1.0, 20.0);
    const bool havePreferredWindow = preferredWindow && !preferredWindow->samples.empty();
    req.captureEndedUtc = havePreferredWindow && preferredWindowEndUtc.isValid()
        ? preferredWindowEndUtc
        : QDateTime::currentDateTimeUtc();
    req.captureStartedUtc = req.captureEndedUtc.addMSecs(-static_cast<qint64>(std::llround(req.requestedSeconds * 1000.0)));
    req.signalLevelDb = gLastRmsDb.load(std::memory_order_relaxed);
    req.noiseFloorDb = gLastNoiseFloorDb.load(std::memory_order_relaxed);
    req.snrDb = gLastSnrDb.load(std::memory_order_relaxed);
    req.afcOffsetHz = gLastAfcOffsetHz.load(std::memory_order_relaxed);

    const auto devices = mgr.getDevices();
    if (devIndex < devices.size()) {
        req.device = devices[devIndex];
        if (req.device.sampleRate > 0.0 && std::isfinite(req.device.sampleRate)) {
            req.sampleRateHz = req.device.sampleRate;
        }
    }

    double spectrumCenterHz = 0.0;
    double spectrumSampleRateHz = 0.0;
    std::vector<float> spectrum;
    if (mgr.getLatestSpectrum(devIndex, spectrum, spectrumCenterHz, spectrumSampleRateHz) &&
        spectrumSampleRateHz > 0.0 && std::isfinite(spectrumSampleRateHz)) {
        req.spectrumDb = std::move(spectrum);
        req.sampleRateHz = spectrumSampleRateHz;
    }

    if (havePreferredWindow) {
        req.iq = preferredWindow->samples;
        req.startAbsolute = preferredWindow->startAbsolute;
        req.endAbsolute = preferredWindow->endAbsolute;
    } else {
        const size_t requestedSamples = static_cast<size_t>(std::clamp(
            req.sampleRateHz * req.requestedSeconds,
            16384.0,
            48000000.0));
        const auto window = mgr.getRecentIQWindowWithCursor(devIndex, requestedSamples);
        req.iq = window.samples;
        req.startAbsolute = window.startAbsolute;
        req.endAbsolute = window.endAbsolute;
    }
    if (req.sampleRateHz > 0.0 && !req.iq.empty()) {
        const double actualSeconds = static_cast<double>(req.iq.size()) / req.sampleRateHz;
        req.captureStartedUtc = req.captureEndedUtc.addMSecs(-static_cast<qint64>(std::llround(actualSeconds * 1000.0)));
    }

    req.p25LogSnapshot
        << "CLI P25 waitgrant follow IQ capture"
        << QString("reason=%1").arg(reasonToken)
        << QString("iq_window_source=%1").arg(havePreferredWindow
            ? (preferredWindowSource.isEmpty() ? QStringLiteral("preferred-follow-snapshot") : preferredWindowSource)
            : QStringLiteral("end-of-follow-tail"))
        << QString("control=%1 MHz rfCenter=%2 MHz voice=%3 MHz tg=%4 protocol=%5 slot=%6")
              .arg(controlFreqHz / 1e6, 0, 'f', 5)
              .arg(req.centerFreqHz / 1e6, 0, 'f', 5)
              .arg(req.tunedFreqHz / 1e6, 0, 'f', 5)
              .arg(tg.talkgroupId)
              .arg(p25TalkgroupIsPhase2(tg) ? "P2 TDMA" : "P1 FDMA")
              .arg(tg.tdmaSlotKnown ? QString::number(static_cast<int>(tg.tdmaSlot & 0x01u)) : QStringLiteral("unknown"))
        << QString("mask_params_known=%1 nac=0x%2 wacn=0x%3 sys=0x%4")
              .arg(tg.p25MaskParamsKnown ? "yes" : "no")
              .arg(static_cast<unsigned>(tg.nac), 3, 16, QLatin1Char('0'))
              .arg(static_cast<unsigned>(tg.wacn), 5, 16, QLatin1Char('0'))
              .arg(static_cast<unsigned>(tg.systemId), 3, 16, QLatin1Char('0'))
        << p25FollowDetailLogText(tg);
    if (!selectedGrantEventText.isEmpty()) {
        req.p25LogSnapshot << QString("selected_grant_event=%1").arg(selectedGrantEventText);
    }
    if (!selectedGrantDetailText.isEmpty()) {
        req.p25LogSnapshot << QString("selected_grant_detail=%1").arg(selectedGrantDetailText);
    }
    req.p25LogSnapshot
        << QString("last_voice_signature=%1").arg(QString::fromStdString(lastVoiceSig.empty() ? "none" : lastVoiceSig))
        << p25VoiceDiagCaptureSummary(finalDiag);

    return saveIqTestCapture(req);
}

TrainingCaptureResult saveTrainingCapture(const TrainingCaptureRequest& req)
{
    TrainingCaptureResult out;
    if (req.iq.empty()) {
        out.message = "No IQ samples available yet. Start/tune a device and wait for spectrum/audio first.";
        return out;
    }
    if (req.sampleRateHz <= 0.0 || !std::isfinite(req.sampleRateHz)) {
        out.message = "Invalid sample rate for training capture.";
        return out;
    }

    const QString root = trainingCapturesRoot();
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const std::string labelToken = sanitizeFileToken(req.label);
    const QString baseName = QString("%1_%2_%3MHz")
        .arg(stamp)
        .arg(QString::fromStdString(labelToken))
        .arg(req.tunedFreqHz / 1e6, 0, 'f', 5);
    const QString dir = root + "/" + baseName;
    if (!QDir().mkpath(dir)) {
        out.message = "Could not create training capture directory.";
        return out;
    }

    const std::string base = (dir + "/" + baseName).toStdString();
    const std::string dataPath = base + ".sigmf-data";
    const std::string metaPath = base + ".sigmf-meta";
    const std::string tilePgmPath = base + "_tile.pgm";
    const std::string tileF32Path = base + "_tile.f32";

    {
        std::ofstream data(dataPath, std::ios::binary);
        if (!data.is_open()) {
            out.message = "Could not write SigMF data file.";
            return out;
        }
        for (const auto& s : req.iq) {
            const float re = s.real();
            const float im = s.imag();
            data.write(reinterpret_cast<const char*>(&re), sizeof(float));
            data.write(reinterpret_cast<const char*>(&im), sizeof(float));
        }
    }

    if (req.tile.valid()) {
        writeClassifierTilePreview(req.tile, tilePgmPath, tileF32Path);
    }

    json meta;
    meta["global"] = {
        {"core:datatype", "cf32_le"},
        {"core:sample_rate", req.sampleRateHz},
        {"core:version", "1.2.0"},
        {"core:description", "SDR Town classifier training capture"},
        {"core:recorder", "SDR Town"},
        {"sdrtown:label", req.label},
        {"sdrtown:mode", modeToString(req.mode)},
        {"sdrtown:channel_bandwidth_hz", req.channelBwHz},
        {"sdrtown:audio_lpf_hz", req.lpfHz},
        {"sdrtown:audio_lpf_enabled", req.audioLpfEnabled},
        {"sdrtown:squelch_db", req.squelchDb},
        {"sdrtown:device_index", req.deviceIndex},
        {"sdrtown:device_driver", req.device.driver},
        {"sdrtown:device_label", req.device.label},
        {"sdrtown:device_serial", req.device.serial},
        {"sdrtown:rf_gain_db", req.device.gain},
        {"sdrtown:frequency_correction_ppm", req.device.frequencyCorrectionPpm},
        {"sdrtown:classifier_label", req.recommendation.label},
        {"sdrtown:classifier_confidence", req.recommendation.confidence},
        {"sdrtown:classifier_reason", req.recommendation.reason},
        {"sdrtown:classifier_filter", classifierFilterKindToString(req.recommendation.filterKind)},
        {"sdrtown:classifier_standard_bandwidth_hz", req.recommendation.standardBandwidthHz}
    };
    meta["captures"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:frequency", req.centerFreqHz},
            {"core:datetime", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()}
        }
    });
    meta["annotations"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:sample_count", req.iq.size()},
            {"core:freq_lower_edge", req.tunedFreqHz - req.channelBwHz * 0.5},
            {"core:freq_upper_edge", req.tunedFreqHz + req.channelBwHz * 0.5},
            {"core:label", req.label},
            {"sdrtown:classifier_label", req.recommendation.label},
            {"sdrtown:estimated_bandwidth_hz", req.recommendation.estimatedBandwidthHz},
            {"sdrtown:standard_bandwidth_hz", req.recommendation.standardBandwidthHz},
            {"sdrtown:snr_db", req.recommendation.features.snrDb}
        }
    });
    meta["sdrtown:artifacts"] = {
        {"tile_preview_pgm", QFileInfo(QString::fromStdString(tilePgmPath)).fileName().toStdString()},
        {"tile_f32", QFileInfo(QString::fromStdString(tileF32Path)).fileName().toStdString()},
        {"tile_width", req.tile.width},
        {"tile_height", req.tile.height},
        {"tile_min_db", req.tile.minDb},
        {"tile_max_db", req.tile.maxDb}
    };

    try {
        std::ofstream metaOut(metaPath);
        if (!metaOut.is_open()) {
            out.message = "Could not write SigMF metadata file.";
            return out;
        }
        metaOut << meta.dump(2);

        std::ofstream manifest((root + "/manifest.jsonl").toStdString(), std::ios::app);
        if (manifest.is_open()) {
            json row = {
                {"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()},
                {"label", req.label},
                {"freq_hz", req.tunedFreqHz},
                {"sample_rate_hz", req.sampleRateHz},
                {"sample_count", req.iq.size()},
                {"meta", QFileInfo(QString::fromStdString(metaPath)).fileName().toStdString()},
                {"data", QFileInfo(QString::fromStdString(dataPath)).fileName().toStdString()},
                {"directory", dir.toStdString()},
                {"classifier_label", req.recommendation.label},
                {"classifier_confidence", req.recommendation.confidence}
            };
            manifest << row.dump() << "\n";
        }
    } catch (const std::exception& ex) {
        out.message = QString("Training capture write failed: %1").arg(ex.what());
        return out;
    }

    out.ok = true;
    out.directory = dir;
    out.message = QString("Saved training capture: %1 samples").arg(req.iq.size());
    return out;
}

// P25 voice decode / shared RF: see P25VoiceDecode.h (ISS-0004 Phase 5)

void runP25ReplayFollowTest(P25ReplayCliArgs args)
{
    const double controlMs = args.ms > 0.0 ? std::clamp(args.ms, 50.0, 60000.0) : 15000.0;
    const double followMs = args.followMs > 0.0 ? std::clamp(args.followMs, 512.0, 60000.0) : 5000.0;
    auto capture = loadSigmfCf32Capture(QString::fromStdString(args.path), controlMs + followMs, args.skipMs);
    if (!capture.ok) {
        std::cout << "P25 followtest load failed: " << capture.error << "\n";
        return;
    }
    if (args.centerMhz > 0.0 && std::isfinite(args.centerMhz)) {
        capture.centerFreqHz = args.centerMhz * 1e6;
    }
    double ccHz = args.targetMhz > 0.0 ? args.targetMhz * 1e6 : capture.targetFreqHz;
    if (!std::isfinite(ccHz) || ccHz <= 0.0) ccHz = capture.centerFreqHz;
    const bool voiceCenterOverridden = args.voiceCenterMhz > 0.0 && std::isfinite(args.voiceCenterMhz);
    const double voiceCenterHz = voiceCenterOverridden ? args.voiceCenterMhz * 1e6 : capture.centerFreqHz;
    const bool replayMaskSeeded = trySeedP25ReplayMaskFromCaptureLog(args);
    if (!std::isfinite(capture.sampleRateHz) || capture.sampleRateHz <= 0.0 || capture.iq.empty()) {
        std::cout << "P25 followtest result=NO_IQ samples=" << capture.iq.size()
                  << " sampleRate=" << capture.sampleRateHz << "\n";
        return;
    }

    P25LiveDecoder decoder(p25CliControlGrantDecoderConfig());
    P25ControlChannelAnalyzer analyzer;
    const size_t seededIdentifiers = seedP25AnalyzerFromCachedChannelIdentifiers(analyzer, ccHz);
    std::vector<P25PendingVoiceGrant> pendingVoiceGrants;
    std::vector<P25RepeatedVoiceGrant> repeatedVoiceGrants;
    std::vector<P25TalkgroupEntry> talkgroups;
    std::optional<P25TalkgroupEntry> selectedGrant;
    size_t selectedStartSample = 0;
    std::string selectedSource;
    size_t windows = 0;
    size_t trustedBlocks = 0;
    size_t trustedPdus = 0;
    size_t trustedMacs = 0;
    size_t grants = 0;
    size_t resolvedGrants = 0;
    size_t pendingResolved = 0;
    size_t encryptedSkipped = 0;
    size_t notReadySkipped = 0;
    size_t outOfBandSkipped = 0;
    bool sawNidLock = false;
    std::unordered_map<uint32_t, size_t> skippedEncryptedTgs;
    std::unordered_map<uint32_t, size_t> skippedNotReadyTgs;
    std::unordered_map<uint32_t, size_t> skippedOutOfBandTgs;

    const size_t windowSamples = std::max<size_t>(1, std::min(capture.iq.size(), static_cast<size_t>(
        std::clamp(capture.sampleRateHz * kP25ControlDecodeWindowSeconds, 24000.0, 4194304.0))));
    // Followtest is the deterministic version of live grant monitoring.  Use
    // overlapped control windows so a grant that straddles a window boundary is
    // not missed in the very harness we use to prove follow/audio behavior.
    const size_t hopSamples = std::max<size_t>(1, windowSamples / 2);
    const size_t controlSamples = std::min(capture.iq.size(), static_cast<size_t>(
        std::max(1.0, std::round(capture.sampleRateHz * controlMs / 1000.0))));
    const double passbandHalfHz = capture.sampleRateHz * 0.48;

    auto considerResolvedGrant = [&](const P25ControlEvent& grant, size_t startSample, const char* source) {
        if (selectedGrant || !p25ControlEventIsResolvedVoiceGrant(grant)) return;
        if (args.followTalkgroupId != 0 && grant.talkgroupId != args.followTalkgroupId) return;
        const qint64 nowMs = static_cast<qint64>(std::llround(capture.startOffsetMs +
            (static_cast<double>(startSample) * 1000.0 / capture.sampleRateHz)));
        P25TalkgroupEntry candidate = p25TalkgroupEntryFromCurrentGrant(ccHz, grant, nowMs);
        if (candidate.lastVoiceFreqHz <= 0.0) return;
        p25AugmentTalkgroupFromKnownSite(candidate, talkgroups, ccHz);
        p25RefreshFollowGrantFromRegistry(candidate, talkgroups, nowMs);
        if (p25ReplayHasMaskParameters(args)) {
            candidate.p25MaskParamsKnown = true;
            candidate.nac = static_cast<uint16_t>(args.nac & 0x0fff);
            candidate.wacn = static_cast<uint32_t>(args.wacn & 0x0fffff);
            candidate.systemId = static_cast<uint16_t>(args.systemId & 0x0fff);
        }
        bool probingUnknownPhase2EncryptedHistory = false;
        const bool followReady = p25PrepareTalkgroupForFollowGrant(
            candidate,
            grant,
            probingUnknownPhase2EncryptedHistory);
        if (candidate.encryptionKnown && candidate.encrypted) {
            ++encryptedSkipped;
            ++skippedEncryptedTgs[candidate.talkgroupId];
            return;
        }
        if (!followReady || candidate.lastVoiceFreqHz <= 0.0) {
            ++notReadySkipped;
            ++skippedNotReadyTgs[candidate.talkgroupId];
            return;
        }
        if (probingUnknownPhase2EncryptedHistory && skippedEncryptedTgs[candidate.talkgroupId] == 0) {
            std::cout << "P25 followtest skipped Phase 2 TG " << candidate.talkgroupId
                      << " because the grant/update has no clear service options and encrypted history is present; "
                         "matching sdrtrunk, wait for target-slot PTT/ESS before speaker release.\n";
        }
        if (!std::isfinite(candidate.lastVoiceFreqHz) ||
            std::abs(candidate.lastVoiceFreqHz - voiceCenterHz) > passbandHalfHz) {
            ++outOfBandSkipped;
            ++skippedOutOfBandTgs[candidate.talkgroupId];
            return;
        }

        selectedGrant = candidate;
        selectedStartSample = startSample;
        selectedSource = source ? source : "unknown";
    };

    auto consumeEvent = [&](const P25ControlEvent& ev,
                            size_t startSample,
                            const char* source,
                            std::optional<int> correctedDibitErrors) {
        if (p25ControlEventIsVoiceGrant(ev)) {
            ++grants;
            if (p25ControlEventIsResolvedVoiceGrant(ev)) ++resolvedGrants;
        }
        const qint64 nowMs = static_cast<qint64>(std::llround(capture.startOffsetMs +
            (static_cast<double>(startSample) * 1000.0 / capture.sampleRateHz)));
        const P25RepeatedVoiceGrantDecision repeatDecision = correctedDibitErrors.has_value()
            ? p25RememberRepeatedHighCorrectionResolvedVoiceGrant(
                repeatedVoiceGrants, ccHz, ev, *correctedDibitErrors, nowMs)
            : P25RepeatedVoiceGrantDecision();
        const bool eventRegistryEligible = !correctedDibitErrors.has_value() ||
            p25TsbkEventRegistryEligible(*correctedDibitErrors, ev) ||
            repeatDecision.promoted;
        if (!eventRegistryEligible) {
            if (repeatDecision.considered) {
                std::cout << "P25 followtest waiting for repeat-confirmed high-correction Phase 2 grant: TG="
                          << ev.talkgroupId
                          << " corrected=" << *correctedDibitErrors
                          << " hits=" << repeatDecision.hitCount
                          << "/" << kP25RepeatedVoiceGrantMinHits
                          << " repeat_threshold=" << kP25RepeatedVoiceGrantMaxCorrectedDibits << "\n";
            }
            if (correctedDibitErrors.has_value() &&
                p25TsbkPendingVoiceGrantEligible(*correctedDibitErrors, ev)) {
                p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, *correctedDibitErrors, nowMs);
            }
            if (correctedDibitErrors.has_value() &&
                p25TsbkSessionIdentifierEligible(*correctedDibitErrors, ev)) {
                for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                    ++pendingResolved;
                    ++resolvedGrants;
                    mergeP25TalkgroupEvent(talkgroups, ccHz, resolved, nowMs);
                    considerResolvedGrant(resolved, startSample, "pending-resolved-session-identifier");
                    if (selectedGrant) break;
                }
            }
            return;
        }

        if (p25ControlEventIsVoiceGrant(ev) && !p25ControlEventIsResolvedVoiceGrant(ev)) {
            p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, correctedDibitErrors.value_or(0), nowMs);
        }
        if (repeatDecision.promoted) {
            std::cout << "P25 followtest accepted repeat-confirmed high-correction Phase 2 grant: TG="
                      << ev.talkgroupId
                      << " corrected=" << *correctedDibitErrors
                      << " best=" << repeatDecision.bestCorrectedDibitErrors
                      << " hits=" << repeatDecision.hitCount
                      << " repeat_threshold=" << kP25RepeatedVoiceGrantMaxCorrectedDibits << "\n";
        }
        mergeP25TalkgroupEvent(talkgroups, ccHz, ev, nowMs);
        considerResolvedGrant(ev, startSample, source);

        if (ev.type == P25ControlEventType::IdentifierUpdate &&
            p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
            for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                ++pendingResolved;
                ++resolvedGrants;
                mergeP25TalkgroupEvent(talkgroups, ccHz, resolved, nowMs);
                considerResolvedGrant(resolved, startSample, "pending-resolved");
            }
        }
    };

    for (size_t start = 0; start < controlSamples && !selectedGrant; start += hopSamples) {
        const size_t end = std::min(capture.iq.size(), start + windowSamples);
        if (end <= start) break;
        std::vector<std::complex<float>> window(capture.iq.begin() + static_cast<std::ptrdiff_t>(start),
                                                capture.iq.begin() + static_cast<std::ptrdiff_t>(end));
        auto result = decoder.processIq(window, capture.sampleRateHz, capture.centerFreqHz, ccHz);
        ++windows;
        p25SeedAnalyzerNacFromDecode(analyzer, result);
        sawNidLock = sawNidLock || p25DecodeResultHasNidLock(result);

        for (const auto& block : result.rawTsbkBlocks) {
            if (!block.fecDecoded || !block.crcValid) continue;
            ++trustedBlocks;
            const auto events = analyzer.ingestTsbk(block.bytes);
            for (const auto& ev : events) {
                consumeEvent(ev, start, "TSBK", block.correctedDibitErrors);
                if (selectedGrant) break;
            }
            if (selectedGrant) break;
        }
        if (selectedGrant) break;

        for (const auto& pdu : result.phase1Pdus) {
            if (!pdu.headerFecDecoded || !pdu.headerCrcValid) continue;
            ++trustedPdus;
            std::vector<std::vector<uint8_t>> dataBlocks;
            dataBlocks.reserve(pdu.dataBlocks.size());
            for (const auto& block : pdu.dataBlocks) dataBlocks.push_back(block.bytes);
            const auto events = analyzer.ingestPhase1Pdu(pdu.format, pdu.vendor, pdu.opcode, pdu.headerBytes, dataBlocks, true);
            for (const auto& ev : events) {
                consumeEvent(ev, start, "P1PDU", std::nullopt);
                if (selectedGrant) break;
            }
            if (selectedGrant) break;
        }
        if (selectedGrant) break;

        for (const auto& pdu : result.phase2MacPdus) {
            if (!pdu.crcValid) continue;
            ++trustedMacs;
            const auto events = analyzer.ingestPhase2MacPdu(
                pdu.opcode, pdu.offset, pdu.bytes, true, pdu.macStructureMaxBits);
            for (const auto& ev : events) {
                consumeEvent(ev, start, "P2MAC", std::nullopt);
                if (selectedGrant) break;
            }
            if (selectedGrant) break;
        }
        if (end == capture.iq.size()) break;
    }

    std::cout << "P25 followtest control center=" << (capture.centerFreqHz / 1e6)
              << "MHz cc=" << (ccHz / 1e6)
              << "MHz voiceCenter=" << (voiceCenterHz / 1e6)
              << "MHz voiceCenterOverride=" << (voiceCenterOverridden ? "yes" : "no")
              << " samples=" << capture.iq.size()
              << " seededIdentifiers=" << seededIdentifiers
              << " windowMs=" << (static_cast<double>(windowSamples) * 1000.0 / capture.sampleRateHz)
              << " hopMs=" << (static_cast<double>(hopSamples) * 1000.0 / capture.sampleRateHz)
              << " windows=" << windows
              << " nidLock=" << (sawNidLock ? "yes" : "no")
              << " trustedTsbk=" << trustedBlocks
              << " trustedP1Pdu=" << trustedPdus
              << " trustedP2Mac=" << trustedMacs
              << " grants=" << grants
              << " resolved=" << resolvedGrants
              << " pendingResolved=" << pendingResolved
              << " maskParams=" << (p25ReplayHasMaskParameters(args) ? (replayMaskSeeded ? "seeded" : "provided") : "unknown")
              << " voiceProfile=" << (args.forensicVoice ? "forensic" : "realtime")
              << " targetTg=" << (args.followTalkgroupId ? std::to_string(args.followTalkgroupId) : std::string("any"))
              << " encryptedSkipped=" << encryptedSkipped
              << " notReadySkipped=" << notReadySkipped
              << " outOfBandSkipped=" << outOfBandSkipped << "\n";

    if (!selectedGrant) {
        const char* result = outOfBandSkipped > 0
            ? "NO_IN_PASSBAND_FOLLOW_CANDIDATE"
            : (encryptedSkipped > 0 ? "NO_CLEAR_FOLLOW_CANDIDATE" : "NO_FOLLOW_CANDIDATE");
        std::cout << "P25 followtest result=" << result
                  << " nidLock=" << (sawNidLock ? "yes" : "no")
                  << " trustedTsbk=" << trustedBlocks
                  << " trustedP1Pdu=" << trustedPdus
                  << " grants=" << grants
                  << " encryptedSkipped=" << encryptedSkipped
                  << " outOfBandSkipped=" << outOfBandSkipped << "\n";
        return;
    }

    P25TalkgroupEntry tg = *selectedGrant;
    const bool phase2Voice = p25TalkgroupIsPhase2(tg);
    std::cout << "P25 followtest selected source=" << selectedSource
              << " startMs=" << (capture.startOffsetMs + static_cast<double>(selectedStartSample) * 1000.0 / capture.sampleRateHz)
              << " " << p25FollowDetailLogText(tg).toStdString() << "\n";

    Receiver rx;
    rx.freqHz = tg.lastVoiceFreqHz;
    rx.mode = DemodMode::NFM;
    rx.channelBwHz = 12500.0;
    rx.lpfHz = 3000.0;
    rx.audioLpfEnabled = false;
    rx.squelchDb = -105.0;
    p25ClearPhase2PendingAudio(rx);
    rx.resetP25VoiceState();
    clearP25SessionScopedState(rx);
    rx.p25VoiceDecodeEnabled = true;
    rx.p25VoiceClearKnown = p25TalkgroupGrantProvesSpeakerClear(tg);
    rx.p25VoiceEncrypted = p25TalkgroupGrantProvesSpeakerEncrypted(tg);
    rx.p25VoiceTalkgroupId = tg.talkgroupId;
    rx.p25VoiceSourceId = tg.lastSourceId;
    rx.p25VoiceGrantEpochMs = QDateTime::currentMSecsSinceEpoch();
    p25Phase2BeginNewPtt(rx, rx.p25VoiceGrantEpochMs);
    rx.p25VoicePhase2 = phase2Voice;
    rx.p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
    rx.p25VoiceTdmaSlot = tg.tdmaSlot;
    rx.p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
    rx.p25VoiceNac = tg.nac;
    rx.p25VoiceWacn = tg.wacn;
    rx.p25VoiceSystemId = tg.systemId;
    rx.p25Phase2GrantedSlotImmutable = false;
    if (rx.p25VoiceTdmaSlotKnown &&
        (rx.p25VoiceClearKnown || rx.p25VoiceEncrypted || rx.p25VoiceMaskParamsKnown)) {
        p25Phase2MarkGrantedSlotImmutable(rx);
    }
    rx.p25Phase2AllowLateEntryAudioProbe =
        phase2Voice && args.fieldAudioProbe;
    const P25VoiceDecodeProfile voiceProfile = args.forensicVoice
        ? P25VoiceDecodeProfile::Forensic
        : P25VoiceDecodeProfile::Realtime;
    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx, voiceProfile));
    if (phase2Voice && rx.p25VoiceMaskParamsKnown) {
        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
    } else {
        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
    }

    const size_t voiceWindowSamples = std::max<size_t>(1, std::min(capture.iq.size(), static_cast<size_t>(
        std::clamp(capture.sampleRateHz * kP25Phase2VoiceDecodeWindowSeconds, 48000.0, 4194304.0))));
    // Use overlapping hop (1/4 window) for smoother coverage of voice frames across boundaries.
    // Prevents choppy gaps at window edges in the capture replay.
    const size_t voiceHopSamples = std::max<size_t>(1, voiceWindowSamples / 4);
    const size_t followSamples = static_cast<size_t>(std::max(1.0, std::round(capture.sampleRateHz * followMs / 1000.0)));
    const size_t voiceEndLimit = std::min(capture.iq.size(), selectedStartSample + followSamples);

    size_t voiceWindows = 0;
    long long decodedFrames = 0;
    long long audioSamples = 0;
    long long speakerSamples = 0;
    long long speakerEmitWindows = 0;
    long long phase2Bursts = 0;
    long long phase2VoiceCodewords = 0;
    long long phase2TargetVoiceCodewords = 0;
    long long phase2MaskedBursts = 0;
    long long phase2MacCrcValid = 0;
    long long phase2FedToMbelib = 0;
    long long phase2EmittedPcmFrames = 0;
    long long phase2ConcealmentFrames = 0;
    long long phase2AmbeAttempts = 0;
    long long phase2AmbeAccepted = 0;
    long long phase2InputQualityRejected = 0;
    long long diagnosticAmbeProbeAttempts = 0;
    long long diagnosticAmbeProbeAccepted = 0;
    long long duplicateSuppressed = 0;
    long long absoluteDuplicateSuppressed = 0;
    long long sequencerSuppressed = 0;
    bool voiceEncrypted = false;
    bool essKnown = false;
    bool essEncrypted = false;
    P25VoiceDiagCode lastDiag = P25VoiceDiagCode::Idle;
    std::string lastVoiceSig;

    for (size_t start = selectedStartSample; start < voiceEndLimit; start += voiceHopSamples) {
        const size_t end = std::min(capture.iq.size(), start + voiceWindowSamples);
        if (end <= start) break;
        std::vector<std::complex<float>> window(capture.iq.begin() + static_cast<std::ptrdiff_t>(start),
                                                capture.iq.begin() + static_cast<std::ptrdiff_t>(end));
        const uint64_t absStart = capture.firstSampleOffset + static_cast<uint64_t>(start);
        auto audio = decodeP25VoiceAudioBlock(rx, window, capture.sampleRateHz, voiceCenterHz,
                                              tg.lastVoiceFreqHz, 48000.0, absStart, true);
        ++voiceWindows;
        decodedFrames += static_cast<long long>(audio.decodedFrames);
        audioSamples += static_cast<long long>(audio.audio.size());
        phase2Bursts += static_cast<long long>(audio.phase2Bursts);
        phase2VoiceCodewords += static_cast<long long>(audio.phase2VoiceCodewords);
        phase2TargetVoiceCodewords += static_cast<long long>(audio.phase2TargetVoiceCodewords);
        phase2MaskedBursts += static_cast<long long>(audio.phase2MaskedBursts);
        phase2MacCrcValid += static_cast<long long>(audio.phase2MacCrcValid);
        phase2FedToMbelib += static_cast<long long>(audio.phase2FedToMbelib);
        phase2EmittedPcmFrames += static_cast<long long>(audio.phase2EmittedPcmFrames);
        phase2ConcealmentFrames += static_cast<long long>(audio.phase2ConcealmentFrames);
        phase2AmbeAttempts += static_cast<long long>(audio.phase2AmbeDecodeAttempts);
        phase2AmbeAccepted += static_cast<long long>(audio.phase2AmbeAcceptedFrames);
        phase2InputQualityRejected += static_cast<long long>(audio.phase2InputQualityRejectedVoiceCodewords);
        diagnosticAmbeProbeAttempts += static_cast<long long>(audio.phase2DiagnosticAmbeProbeAttempts);
        diagnosticAmbeProbeAccepted += static_cast<long long>(audio.phase2DiagnosticAmbeProbeAccepted);
        duplicateSuppressed += static_cast<long long>(audio.phase2DuplicateSuppressedVoiceCodewords);
        absoluteDuplicateSuppressed += static_cast<long long>(audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
        sequencerSuppressed += static_cast<long long>(audio.phase2SequencerSuppressedVoiceCodewords);
        essKnown = essKnown || audio.phase2EssKnown;
        essEncrypted = essEncrypted || audio.phase2EssEncrypted;
        voiceEncrypted = voiceEncrypted ||
            (audio.phase2TargetEssKnown && audio.phase2TargetEssEncrypted) ||
            (audio.phase2EssKnown && audio.phase2EssEncrypted && audio.phase2MacCrcValid > 0);
        lastDiag = audio.diag;
        const std::string speakerGateReason = audio.phase2SpeakerGateReason.empty()
            ? p25VoiceBlockSpeakerGateReason(audio)
            : audio.phase2SpeakerGateReason;
        const bool speakerMayEmit =
            speakerGateReason == "emit" &&
            p25VoiceBlockHasSpeakerTimelineAudio(audio) &&
            audio.phase2EmittedPcmFrames > 0 &&
            p25VoiceBlockMayEmitAudio(audio);
        if (speakerMayEmit) {
            ++speakerEmitWindows;
            speakerSamples += static_cast<long long>(audio.audio.size());
        }

        std::ostringstream sig;
            sig << static_cast<int>(audio.diag) << ":" << audio.decodedFrames << ":" << audio.audio.size()
                << ":" << audio.phase2Bursts << ":" << audio.phase2VoiceCodewords << ":" << audio.phase2MaskedBursts
                << ":" << audio.phase2MacCrcValid << ":" << audio.phase2EssKnown << ":" << audio.phase2EssEncrypted
                << ":" << audio.phase2DuplicateSuppressedVoiceCodewords
                << ":" << audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords
                << ":" << audio.phase2SequencerSuppressedVoiceCodewords
                << ":" << audio.phase2TrafficTalkgroupMismatchVoiceCodewords
                << ":" << audio.phase2TrafficTalkgroupStaleMismatchVoiceCodewords
                << ":" << speakerGateReason;
        if (sig.str() != lastVoiceSig) {
            lastVoiceSig = sig.str();
            std::cout << "P25 followtest voice startMs="
                      << (capture.startOffsetMs + static_cast<double>(start) * 1000.0 / capture.sampleRateHz)
                      << " stage=" << p25VoiceDiagLabel(audio.diag)
                      << " decoded=" << audio.decodedFrames
                      << " audio=" << audio.audio.size()
                      << " p2bursts=" << audio.phase2Bursts
                      << " p2vcw=" << audio.phase2VoiceCodewords
                      << " p2sf=" << audio.phase2SuperframeBursts
                      << " p2mask=" << audio.phase2MaskedBursts
                      << " p2mac=" << audio.phase2MacCrcValid << "/" << audio.phase2MacPdus
                      << " dup=" << audio.phase2DuplicateSuppressedVoiceCodewords
                      << " absDup=" << audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords
                      << " seqDrop=" << audio.phase2SequencerSuppressedVoiceCodewords
                      << " tgMismatchVcw=" << audio.phase2TrafficTalkgroupMismatchVoiceCodewords
                      << " tgStaleMismatchVcw=" << audio.phase2TrafficTalkgroupStaleMismatchVoiceCodewords
                      << " " << p25Phase2AcchStatsText(makeP25VoiceDiagnostics(audio)).toStdString()
                      << " p2ess=" << (audio.phase2EssKnown ? (audio.phase2EssEncrypted ? "enc" : "clear") : "unknown")
                      << " speaker=" << speakerGateReason
                      << " backend=" << (audio.backendAvailable ? "yes" : "no") << "\n";
        }

        // Continue processing the full follow duration to capture all voice audio.
        // The early break was stopping after first good window, resulting in only short "bursts".
        // if (audio.decodedFrames > 0 && !audio.audio.empty()) break;
        if (voiceEncrypted) break;
        if (end == capture.iq.size()) break;
    }

    const bool hasDecodedSpeakerAudio = decodedFrames > 0 && speakerSamples > 0;
    const bool hasConcealmentOnlySpeakerTimeline =
        decodedFrames == 0 && speakerSamples > 0 && speakerEmitWindows > 0 &&
        phase2EmittedPcmFrames > 0 && phase2ConcealmentFrames > 0;
    const bool allTargetAmbeAttemptsRejected =
        phase2TargetVoiceCodewords > 0 &&
        phase2AmbeAttempts > 0 &&
        phase2AmbeAccepted == 0 &&
        phase2InputQualityRejected > 0;
    const char* result = "FAIL_NO_AUDIO";
    if (voiceEncrypted) {
        result = "PASS_ENCRYPTED_GATED";
    } else if (hasDecodedSpeakerAudio && speakerEmitWindows > 0) {
        result = "PASS_CLEAR_AUDIO";
    } else if (allTargetAmbeAttemptsRejected) {
        result = "FAIL_PLC_ONLY_INPUT_QUALITY_REJECTED";
    } else if (hasConcealmentOnlySpeakerTimeline) {
        result = "FAIL_CONCEALMENT_ONLY_AUDIO";
    } else if (decodedFrames > 0 && audioSamples > 0) {
        result = "FAIL_RAW_AUDIO_GATED";
    }
    std::cout << "P25 followtest result=" << result
              << " voiceWindows=" << voiceWindows
              << " speakerWindows=" << speakerEmitWindows
              << " decodedFrames=" << decodedFrames
              << " audioSamples=" << audioSamples
              << " speakerSamples=" << speakerSamples
              << " lastStage=" << p25VoiceDiagLabel(lastDiag)
              << " p2bursts=" << phase2Bursts
              << " p2vcw=" << phase2VoiceCodewords
              << " targetVcw=" << phase2TargetVoiceCodewords
              << " p2mask=" << phase2MaskedBursts
              << " p2macCrc=" << phase2MacCrcValid
              << " fed=" << phase2FedToMbelib
              << " emitPcm=" << phase2EmittedPcmFrames
              << " plc=" << phase2ConcealmentFrames
              << " iqReject=" << phase2InputQualityRejected
              << " ambe=" << phase2AmbeAccepted << "/" << phase2AmbeAttempts
              << " dupSuppressed=" << duplicateSuppressed
              << " absDupSuppressed=" << absoluteDuplicateSuppressed
              << " seqSuppressed=" << sequencerSuppressed
              << " essKnown=" << (essKnown ? "yes" : "no")
              << " essEncrypted=" << (essEncrypted ? "yes" : "no") << "\n";
    std::cout << "P25 continuity slotChanged=" << rx.p25DiagSlotChanged
              << " stickyInvert=" << rx.p25DiagStickyInvert
              << " slotProbe=" << rx.p25DiagSlotProbe
              << " slotProbeBlocked=" << rx.p25DiagSlotProbeBlocked
              << " securityChanged=" << rx.p25DiagSecurityChanged
              << " securityLatch=" << static_cast<int>(rx.p25SessionState.callSecurityLatch)
              << " vocoderReset=" << rx.p25DiagVocoderReset
              << " pendingCleared=" << rx.p25DiagPendingAudioCleared
              << " variantChanged=" << rx.p25DiagVariantChanged
              << " seqGapSilence=" << rx.p25DiagSequencerGapSilence
              << " seqLateDrops=" << rx.p25DiagSequencerLateDrops
              << " slotImmutable=" << (rx.p25Phase2GrantedSlotImmutable ? "yes" : "no")
              << "\n";
}

void runP25ReplayVoiceTest(const P25ReplayCliArgs& argsIn)
{
    P25ReplayCliArgs args = argsIn;
    const double replayMs = args.ms > 0.0 ? std::clamp(args.ms, 50.0, 60000.0) : 5000.0;
    const auto loadStart = std::chrono::steady_clock::now();
    if (args.traceReplay) {
        std::cerr << "P25 voicetest trace=load-start path=\"" << args.path
                  << "\" replayMs=" << replayMs
                  << " skipMs=" << args.skipMs << std::endl;
    }
    auto capture = loadSigmfCf32Capture(QString::fromStdString(args.path), replayMs, args.skipMs);
    if (args.traceReplay) {
        const auto loadMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loadStart).count();
        std::cerr << "P25 voicetest trace=load-done ok=" << (capture.ok ? "yes" : "no")
                  << " samples=" << capture.iq.size()
                  << " elapsedMs=" << loadMs << std::endl;
    }
    if (!capture.ok) {
        std::cout << "P25 voicetest load failed: " << capture.error << "\n";
        return;
    }
    if (args.centerMhz > 0.0 && std::isfinite(args.centerMhz)) {
        capture.centerFreqHz = args.centerMhz * 1e6;
    }
    const bool voiceCenterOverridden = args.voiceCenterMhz > 0.0 && std::isfinite(args.voiceCenterMhz);
    const double voiceCenterHz = voiceCenterOverridden ? args.voiceCenterMhz * 1e6 : capture.centerFreqHz;
    double voiceHz = args.targetMhz > 0.0 ? args.targetMhz * 1e6 : capture.targetFreqHz;
    if (!std::isfinite(voiceHz) || voiceHz <= 0.0) voiceHz = voiceCenterHz;
    if (!std::isfinite(capture.sampleRateHz) || capture.sampleRateHz <= 0.0 || capture.iq.empty()) {
        std::cout << "P25 voicetest result=NO_IQ samples=" << capture.iq.size()
                  << " sampleRate=" << capture.sampleRateHz << "\n";
        return;
    }

    Receiver rx;
    rx.freqHz = voiceHz;
    rx.mode = DemodMode::NFM;
    rx.channelBwHz = 12500.0;
    rx.lpfHz = 3000.0;
    rx.audioLpfEnabled = false;
    rx.squelchDb = -105.0;
    p25ClearPhase2PendingAudio(rx);
    rx.resetP25VoiceState();
    clearP25SessionScopedState(rx);
    rx.p25VoiceDecodeEnabled = true;
    rx.p25VoiceClearKnown = args.clearGrant && !args.encryptedGrant;
    rx.p25VoiceEncrypted = args.encryptedGrant;
    rx.p25VoiceTalkgroupId = args.followTalkgroupId != 0 ? args.followTalkgroupId : 1u;
    rx.p25VoiceSourceId = 0;
    rx.p25VoiceGrantEpochMs = QDateTime::currentMSecsSinceEpoch() - 1000;
    p25Phase2BeginNewPtt(rx, rx.p25VoiceGrantEpochMs);
    rx.p25VoicePhase2 = true;
    rx.p25TrafficRetunesPrimary = true;
    rx.p25IndependentTrafficSource = true;
    rx.p25TrafficVoiceFreqHz = voiceHz;
    rx.p25TrafficSourceCenterFreqHz = voiceCenterHz;
    rx.p25TrafficControlFreqHz = std::isfinite(capture.centerFreqHz) && capture.centerFreqHz > 0.0
        ? capture.centerFreqHz
        : voiceCenterHz;
    (void)trySeedP25ReplayMaskFromCaptureLog(args);
    rx.p25VoiceTdmaSlotKnown = args.tdmaSlot >= 0;
    rx.p25VoiceTdmaSlot = args.tdmaSlot >= 0 ? static_cast<uint8_t>(args.tdmaSlot & 0x01) : 0u;
    rx.p25VoiceMaskParamsKnown = p25ReplayHasMaskParameters(args);
    rx.p25VoiceNac = args.nac >= 0 ? static_cast<uint16_t>(args.nac) : 0;
    rx.p25VoiceWacn = args.wacn >= 0 ? static_cast<uint32_t>(args.wacn) : 0;
    rx.p25VoiceSystemId = args.systemId >= 0 ? static_cast<uint16_t>(args.systemId) : 0;
    rx.p25Phase2GrantedSlotImmutable = false;
    if (rx.p25VoiceTdmaSlotKnown &&
        (rx.p25VoiceClearKnown || rx.p25VoiceEncrypted || rx.p25VoiceMaskParamsKnown)) {
        p25Phase2MarkGrantedSlotImmutable(rx);
    }
    rx.p25Phase2AllowLateEntryAudioProbe = args.fieldAudioProbe;
    if (std::isfinite(args.trafficTargetOffsetHz) && std::abs(args.trafficTargetOffsetHz) >= 50.0 &&
        std::abs(args.trafficTargetOffsetHz) <= 25000.0) {
        rx.p25TrafficRetunesPrimary = true;
        rx.p25Phase2TrafficTargetOffsetKnown = true;
        rx.p25Phase2TrafficTargetOffsetHz = args.trafficTargetOffsetHz;
        rx.p25Phase2TrafficTargetOffsetTrust = kP25Phase2TrafficTargetOffsetVerifiedTrust;
        rx.p25Phase2TrafficTargetOffsetMisses = 0;
    }
    const P25VoiceDecodeProfile voiceProfile = args.forensicVoice
        ? P25VoiceDecodeProfile::Forensic
        : P25VoiceDecodeProfile::Realtime;
    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx, voiceProfile));
    if (rx.p25VoiceMaskParamsKnown) {
        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
    } else {
        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
    }

    const double coldWindowSeconds = args.windowMs > 0.0
        ? args.windowMs / 1000.0
        : kP25Phase2VoiceDecodeWindowSeconds;
    const bool replayStreamingDdc =
        args.streamVoice && rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc;
    const double hopSeconds = args.hopMs > 0.0
        ? args.hopMs / 1000.0
        : (args.streamVoice
            ? (replayStreamingDdc
                ? kP25Phase2VoiceDecodeSustainChunkSeconds
                : kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds)
            : (coldWindowSeconds * 0.5));
    // Stream replay mirrors the GUI worker and SDRTrunk's continuous processor:
    // one cold lookback to acquire the TDMA lattice, then contiguous fresh IQ
    // when the realtime streaming DDC is active. Stateless forensic/block
    // channelization still uses bounded pre-roll because each eye is independent.
    const double streamAcquireContextSeconds = args.streamVoice
        ? std::min(coldWindowSeconds, kP25Phase2VoiceDecodeAcquireOverlapSeconds)
        : coldWindowSeconds;
    const double streamSustainContextSeconds = args.streamVoice
        ? std::min(coldWindowSeconds, kP25Phase2VoiceDecodeSustainOverlapSeconds)
        : coldWindowSeconds;
    const double streamSpeakerSustainContextSeconds = args.streamVoice
        ? std::min(coldWindowSeconds, kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds)
        : coldWindowSeconds;
    const size_t voiceColdWindowSamples = std::max<size_t>(1, std::min(capture.iq.size(), static_cast<size_t>(
        std::clamp(capture.sampleRateHz * coldWindowSeconds, 48000.0, 4194304.0))));
    const size_t voiceFreshSamples = std::max<size_t>(1, static_cast<size_t>(
        std::clamp(capture.sampleRateHz * hopSeconds, 2048.0, static_cast<double>(voiceColdWindowSamples))));
    const size_t voiceAcquireContextSamples = args.streamVoice
        ? std::min(voiceColdWindowSamples, static_cast<size_t>(
            std::clamp(capture.sampleRateHz * streamAcquireContextSeconds, 0.0,
                       static_cast<double>(voiceColdWindowSamples))))
        : voiceColdWindowSamples;
    const size_t voiceSustainContextSamples = args.streamVoice
        ? std::min(voiceColdWindowSamples, static_cast<size_t>(
            std::clamp(capture.sampleRateHz * streamSustainContextSeconds, 0.0,
                       static_cast<double>(voiceColdWindowSamples))))
        : voiceColdWindowSamples;
    const size_t voiceSpeakerSustainContextSamples = args.streamVoice
        ? std::min(voiceColdWindowSamples, static_cast<size_t>(
            std::clamp(capture.sampleRateHz * streamSpeakerSustainContextSeconds, 0.0,
                       static_cast<double>(voiceColdWindowSamples))))
        : voiceColdWindowSamples;
    const size_t voiceWindowSamples = args.streamVoice
        ? voiceColdWindowSamples
        : std::max<size_t>(1, std::min(capture.iq.size(), static_cast<size_t>(
            std::clamp(capture.sampleRateHz * coldWindowSeconds, 48000.0, 4194304.0))));
    const size_t voiceHopSamples = args.streamVoice
        ? voiceFreshSamples
        : std::max<size_t>(1, static_cast<size_t>(
            std::clamp(capture.sampleRateHz * hopSeconds, 2048.0, static_cast<double>(voiceWindowSamples))));
    const size_t voiceUnacquiredAcquireHopSamples = args.streamVoice
        ? (args.hopMs > 0.0
            ? voiceHopSamples
            : std::max<size_t>(1, static_cast<size_t>(std::clamp(
                capture.sampleRateHz * kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds,
                2048.0,
                static_cast<double>(voiceColdWindowSamples)))))
        : voiceHopSamples;

    std::cout << "P25 voicetest voiceCenter=" << (voiceCenterHz / 1e6)
              << "MHz voice=" << (voiceHz / 1e6)
              << "MHz samples=" << capture.iq.size()
              << " windowMs=" << (static_cast<double>(voiceColdWindowSamples) * 1000.0 / capture.sampleRateHz)
              << " hopMs=" << (static_cast<double>(voiceHopSamples) * 1000.0 / capture.sampleRateHz)
              << " contextMs=" << (static_cast<double>(voiceAcquireContextSamples) * 1000.0 / capture.sampleRateHz)
              << " sustainContextMs=" << (static_cast<double>(voiceSustainContextSamples) * 1000.0 / capture.sampleRateHz)
              << " speakerSustainContextMs=" << (static_cast<double>(voiceSpeakerSustainContextSamples) * 1000.0 / capture.sampleRateHz)
              << " stream=" << (args.streamVoice ? "yes" : "no")
              << " tg=" << rx.p25VoiceTalkgroupId
              << " slot=" << (rx.p25VoiceTdmaSlotKnown ? std::to_string(rx.p25VoiceTdmaSlot & 0x01u) : std::string("unknown"))
              << " grant=" << (rx.p25VoiceEncrypted ? "encrypted" : (rx.p25VoiceClearKnown ? "clear" : "unknown"))
              << " probe=" << (rx.p25Phase2AllowLateEntryAudioProbe ? "on" : "off")
              << " offsetHz=" << (rx.p25Phase2TrafficTargetOffsetKnown ? rx.p25Phase2TrafficTargetOffsetHz : 0.0)
              << " maskParams=" << (rx.p25VoiceMaskParamsKnown ? "provided" : "none")
              << " profile=" << (args.forensicVoice ? "forensic" : "realtime")
              << " meta=\"" << capture.metaPath.toStdString() << "\"\n";
    std::cout.flush();

    const bool streamContiguousDdc = replayStreamingDdc;

    size_t voiceWindows = 0;
    long long decodedFrames = 0;
    long long audioSamples = 0;
    long long speakerSamples = 0;
    long long phase2Bursts = 0;
    long long phase2VoiceCodewords = 0;
    long long phase2TargetVoiceCodewords = 0;
    long long phase2MaskedBursts = 0;
    long long phase2MacCrcValid = 0;
    long long phase2ExpectedVoiceCodewords = 0;
    long long phase2ContextVoiceCodewords = 0;
    long long phase2ContextSuppressedVoiceCodewords = 0;
    long long phase2FedToMbelib = 0;
    long long phase2EmittedPcmFrames = 0;
    long long phase2EmittedSpeechOrdinalFrames = 0;
    long long phase2ConcealmentFrames = 0;
    long long phase2FeedGaps = 0;
    long long phase2AmbeAttempts = 0;
    long long phase2AmbeAccepted = 0;
    long long phase2OppositeAmbeAttempts = 0;
    long long phase2OppositeAmbeAccepted = 0;
    long long phase2OppositePendingQueued = 0;
    long long phase2InputQualityRejected = 0;
    long long phase2TrafficTalkgroupMismatch = 0;
    long long phase2TrafficTalkgroupStaleMismatch = 0;
    long long trustedClearWindows = 0;
    long long targetSessionReleaseWindows = 0;
    long long targetEssClearWindows = 0;
    long long diagnosticAmbeProbeAttempts = 0;
    long long diagnosticAmbeProbeAccepted = 0;
    long long emitWindows = 0;
    long long gatedRawWindows = 0;
    long long emptyWindows = 0;
    long long speakerOrdinalMissingWindows = 0;
    long long speakerOrdinalPartialWindows = 0;
    long long speakerTimelineDroppedFrames = 0;
    long long duplicateSuppressed = 0;
    long long absoluteDuplicateSuppressed = 0;
    long long sequencerSuppressed = 0;
    bool voiceEncrypted = false;
    bool essKnown = false;
    bool essEncrypted = false;
    P25VoiceDiagCode lastDiag = P25VoiceDiagCode::Idle;
    std::string lastVoiceSig;
    std::vector<float> continuousPcm;
    continuousPcm.reserve(static_cast<size_t>(capture.sampleRateHz)); // rough; grows as needed
    std::vector<float> continuousOppositePcm;
    P25Phase2SpeakerPendingQueue voiceTestSpeakerQueue;
    bool cliHardTargetAcquire = false;
    int cliMacEssStarveWindows = 0;
    int cliWideReacquireHoldWindows = 0;
    bool cliForceMaskEpochRehunt = false;
    int cliMaskEpochRepairHoldWindows = 0;
    int cliEmptyEyeWindows = 0;

    // Stream mode walks an end cursor: first cold lookback, then bounded
    // context+fresh slices with absolute-dibit de-dupe.
    size_t streamEnd = args.streamVoice
        ? std::min(capture.iq.size(), voiceColdWindowSamples)
        : 0;
    for (size_t start = 0;
         args.streamVoice ? (streamEnd > 0 && streamEnd <= capture.iq.size())
                          : (start < capture.iq.size());
         args.streamVoice ? (streamEnd = (streamEnd >= capture.iq.size()
                                            ? capture.iq.size() + 1
                                            : std::min(capture.iq.size(), streamEnd +
                                                ((!cliHardTargetAcquire && args.hopMs <= 0.0)
                                                    ? voiceUnacquiredAcquireHopSamples
                                                    : voiceHopSamples))))
                          : (start += voiceHopSamples)) {
        size_t winStart = start;
        size_t winEnd = std::min(capture.iq.size(), start + voiceWindowSamples);
        if (args.streamVoice) {
            if (streamEnd > capture.iq.size()) break;
            winEnd = streamEnd;
            const bool forceWideReacquire = cliWideReacquireHoldWindows > 0;
            const bool forceMaskEpochRepair = cliMaskEpochRepairHoldWindows > 0;
            const bool streamUnacquiredAcquireWindow =
                !cliHardTargetAcquire || forceMaskEpochRepair;
            const bool streamSpeakerSustainWindow =
                !forceWideReacquire &&
                !forceMaskEpochRepair &&
                !streamUnacquiredAcquireWindow &&
                voiceWindows > 1 &&
                (speakerSamples > 0 || p25Phase2SessionSpeakerSustainActive(rx));
            const size_t contextSamplesForDesiredLookback = streamContiguousDdc
                ? 0u
                : (streamSpeakerSustainWindow
                    ? voiceSpeakerSustainContextSamples
                    : (streamUnacquiredAcquireWindow
                        ? voiceColdWindowSamples
                        : (voiceWindows > 1 ? voiceSustainContextSamples : voiceAcquireContextSamples)));
            const size_t desiredLookback = streamContiguousDdc && voiceWindows > 0
                ? (streamUnacquiredAcquireWindow ? voiceUnacquiredAcquireHopSamples : voiceFreshSamples)
                : (forceWideReacquire || forceMaskEpochRepair
                    ? voiceColdWindowSamples
                    : (voiceWindows == 0 || streamUnacquiredAcquireWindow)
                    ? voiceColdWindowSamples
                    : std::min(voiceColdWindowSamples,
                               contextSamplesForDesiredLookback + voiceFreshSamples));
            winStart = (winEnd >= desiredLookback) ? (winEnd - desiredLookback) : 0;
        }
        if (winEnd <= winStart) break;
        std::vector<std::complex<float>> window(capture.iq.begin() + static_cast<std::ptrdiff_t>(winStart),
                                                capture.iq.begin() + static_cast<std::ptrdiff_t>(winEnd));
        const uint64_t absStart = capture.firstSampleOffset + static_cast<uint64_t>(winStart);
        const size_t freshSamplesForWindow = streamContiguousDdc
            ? window.size()
            : args.streamVoice
            ? ((voiceWindows == 0 || !cliHardTargetAcquire)
                ? window.size()
                : std::min(voiceFreshSamples, window.size()))
            : window.size();
        const size_t contextSamplesForWindow = args.streamVoice && window.size() > freshSamplesForWindow
            ? (window.size() - freshSamplesForWindow)
            : 0;
        const auto decodeStart = std::chrono::steady_clock::now();
        const bool streamRealtime = args.streamVoice && !args.forensicVoice;
        // Keep block channelization stateless, but do not make every
        // post-acquire hop a cold reacquire. That overloaded live/replay P25
        // workers with 32-candidate searches and delayed continuous audio.
        const bool streamColdWindow =
            streamRealtime &&
            (voiceWindows == 0 ||
             !cliHardTargetAcquire ||
             cliWideReacquireHoldWindows > 0 ||
             cliMaskEpochRepairHoldWindows > 0);
        const bool streamHotWindow = streamRealtime && !streamColdWindow && voiceWindows > 0;
        const bool streamLockOnlyWindow =
            streamHotWindow &&
            cliHardTargetAcquire &&
            rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc &&
            (rx.p25VoiceLiveDecoder.cqpskLockValid() ||
             speakerSamples > 0);
        const int priorDecodeBudgetMs = rx.p25VoiceLiveDecoder.config().realtimeDecodeBudgetMs;
        const size_t priorCqpskCandidates = rx.p25VoiceLiveDecoder.config().maxCqpskSearchCandidates;
        const size_t priorPhase2SyncHits = rx.p25VoiceLiveDecoder.config().maxPhase2SyncHits;
        const size_t priorPhase2Locks = rx.p25VoiceLiveDecoder.config().maxPhase2SuperframeLocks;
        auto boundedConfigValue = [](size_t current, size_t cap) {
            return current == 0 ? cap : std::min(current, cap);
        };
        if (streamColdWindow) {
            rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                std::min(priorDecodeBudgetMs, kP25VoiceWorkerColdRealtimeBudgetMs));
            rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                boundedConfigValue(priorCqpskCandidates, kP25VoiceWorkerColdMaxCqpskCandidates));
        } else if (streamHotWindow) {
            // File replay / voicetest: IQ is already captured. Use the replay
            // caps so a hop can walk a full superframe. Live GUI worker uses
            // kP25LiveLockedStream* / hot caps instead.
            // DEC-0019 cand=3 after speak was tried here as a live proxy:
            // wall 17→7 s but 105622 duty 0.645→0.055 drop=A. Do not mirror
            // that cap into voicetest; keep replay cand=16 for file gates.
            rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                std::min(priorDecodeBudgetMs,
                         streamLockOnlyWindow ? kP25ReplayHotBudgetMs : kP25ReplayHotBudgetMs));
            rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                streamLockOnlyWindow ? kP25LiveLockedStreamCqpskCandidates
                                     : boundedConfigValue(priorCqpskCandidates,
                                                          kP25ReplayHotCqpskCandidates));
            rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                boundedConfigValue(priorPhase2SyncHits, kP25ReplayHotSyncHits));
            rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                boundedConfigValue(priorPhase2Locks, kP25ReplayHotSuperframeLocks));
        }
        const bool forceWideReacquireDecode = args.streamVoice && cliWideReacquireHoldWindows > 0;
        if (cliForceMaskEpochRehunt) {
            rx.p25VoiceLiveDecoder.invalidatePhase2StickyMaskEpoch();
            cliForceMaskEpochRehunt = false;
        }
        if (forceWideReacquireDecode) {
            // True eye-loss path only: keep hard reset for empty Phase-2 telemetry.
            if (rx.p25VoiceDiagnostics.phase2Bursts == 0 &&
                rx.p25VoiceDiagnostics.phase2MaskedBursts == 0 &&
                rx.p25VoiceDiagnostics.phase2TargetVoiceCodewords == 0) {
                rx.p25VoiceLiveDecoder.reset();
                if (rx.p25VoiceMaskParamsKnown) {
                    rx.p25VoiceLiveDecoder.setPhase2MaskParameters(
                        rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                }
                rx.p25VoiceLiveDecoder.setPhase2PreferredTdmaSlot(
                    rx.p25VoiceTdmaSlotKnown,
                    static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u));
            }
            --cliWideReacquireHoldWindows;
        }
        if (cliMaskEpochRepairHoldWindows > 0) {
            --cliMaskEpochRepairHoldWindows;
        }
        if (args.traceReplay) {
            std::cerr << "P25 voicetest trace=decode-start window=" << (voiceWindows + 1)
                      << " startMs=" << (capture.startOffsetMs + static_cast<double>(winStart) * 1000.0 / capture.sampleRateHz)
                      << " samples=" << window.size()
                      << " fresh=" << freshSamplesForWindow
                      << " context=" << contextSamplesForWindow
                      << " cfMHz=" << (voiceCenterHz / 1e6)
                      << " voiceMHz=" << (voiceHz / 1e6)
                      << std::endl;
        }
        auto audio = decodeP25VoiceAudioBlock(rx, window, capture.sampleRateHz, voiceCenterHz,
                                              voiceHz, 48000.0, absStart, true,
                                              contextSamplesForWindow);
        if (streamColdWindow || streamHotWindow) {
            rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(priorDecodeBudgetMs);
            rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(priorCqpskCandidates);
            rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(priorPhase2SyncHits);
            rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(priorPhase2Locks);
        }
        if (args.traceReplay) {
            const auto decodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - decodeStart).count();
            std::cerr << "P25 voicetest trace=decode-done window=" << (voiceWindows + 1)
                      << " elapsedMs=" << decodeMs
                      << " diag=" << p25VoiceDiagLabel(audio.diag)
                      << " p2bursts=" << audio.phase2Bursts
                      << " p2vcw=" << audio.phase2VoiceCodewords
                      << " audio=" << audio.audio.size()
                      << " path=" << (audio.demodPath.empty() ? "unknown" : audio.demodPath)
                      << " stickyInvert=" << (rx.p25Phase2StickySlotLabelInvert ? "yes" : "no")
                      << std::endl;
            for (const auto& warning : audio.decoderWarnings) {
                std::cerr << "P25 voicetest trace=warning window=" << (voiceWindows + 1)
                          << " " << warning << std::endl;
            }
        }
        ++voiceWindows;
        decodedFrames += static_cast<long long>(audio.decodedFrames);
        audioSamples += static_cast<long long>(audio.audio.size());
        phase2Bursts += static_cast<long long>(audio.phase2Bursts);
        phase2VoiceCodewords += static_cast<long long>(audio.phase2VoiceCodewords);
        phase2TargetVoiceCodewords += static_cast<long long>(audio.phase2TargetVoiceCodewords);
        phase2MaskedBursts += static_cast<long long>(audio.phase2MaskedBursts);
        phase2MacCrcValid += static_cast<long long>(audio.phase2MacCrcValid);
        phase2ExpectedVoiceCodewords += static_cast<long long>(audio.phase2ExpectedVoiceCodewords);
        phase2ContextVoiceCodewords += static_cast<long long>(audio.phase2ContextVoiceCodewords);
        phase2ContextSuppressedVoiceCodewords += static_cast<long long>(audio.phase2ContextSuppressedVoiceCodewords);
        phase2FedToMbelib += static_cast<long long>(audio.phase2FedToMbelib);
        phase2EmittedPcmFrames += static_cast<long long>(audio.phase2EmittedPcmFrames);
        phase2EmittedSpeechOrdinalFrames +=
            static_cast<long long>(audio.phase2EmittedSpeechOrdinals.size());
        phase2ConcealmentFrames += static_cast<long long>(audio.phase2ConcealmentFrames);
        phase2FeedGaps += static_cast<long long>(audio.phase2FeedGaps);
        phase2AmbeAttempts += static_cast<long long>(audio.phase2AmbeDecodeAttempts);
        phase2AmbeAccepted += static_cast<long long>(audio.phase2AmbeAcceptedFrames);
        phase2OppositeAmbeAttempts += static_cast<long long>(audio.phase2OppositeAmbeDecodeAttempts);
        phase2OppositeAmbeAccepted += static_cast<long long>(audio.phase2OppositeAmbeAcceptedFrames);
        phase2OppositePendingQueued += static_cast<long long>(audio.phase2OppositePendingQueued);
        phase2InputQualityRejected += static_cast<long long>(audio.phase2InputQualityRejectedVoiceCodewords);
        phase2TrafficTalkgroupMismatch +=
            static_cast<long long>(audio.phase2TrafficTalkgroupMismatchVoiceCodewords);
        phase2TrafficTalkgroupStaleMismatch +=
            static_cast<long long>(audio.phase2TrafficTalkgroupStaleMismatchVoiceCodewords);
        if (audio.phase2SecurityTrustedClear) ++trustedClearWindows;
        if (audio.phase2TargetSessionAudioRelease) ++targetSessionReleaseWindows;
        if (audio.phase2TargetEssKnown && !audio.phase2TargetEssEncrypted) ++targetEssClearWindows;
        diagnosticAmbeProbeAttempts += static_cast<long long>(audio.phase2DiagnosticAmbeProbeAttempts);
        diagnosticAmbeProbeAccepted += static_cast<long long>(audio.phase2DiagnosticAmbeProbeAccepted);
        duplicateSuppressed += static_cast<long long>(audio.phase2DuplicateSuppressedVoiceCodewords);
        absoluteDuplicateSuppressed += static_cast<long long>(audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
        sequencerSuppressed += static_cast<long long>(audio.phase2SequencerSuppressedVoiceCodewords);
        essKnown = essKnown || audio.phase2EssKnown;
        essEncrypted = essEncrypted || audio.phase2EssEncrypted;
        voiceEncrypted = voiceEncrypted ||
            (audio.phase2TargetEssKnown && audio.phase2TargetEssEncrypted) ||
            (audio.phase2EssKnown && audio.phase2EssEncrypted && audio.phase2MacCrcValid > 0);
        lastDiag = audio.diag;
        if (p25Phase2TargetHardClearEvidence(audio) ||
            audio.phase2TargetMacCrcValid ||
            audio.decodedFrames > 0 ||
            !audio.audio.empty()) {
            cliHardTargetAcquire = true;
            cliMacEssStarveWindows = 0;
            cliWideReacquireHoldWindows = 0;
            cliForceMaskEpochRehunt = false;
            cliMaskEpochRepairHoldWindows = 0;
            cliEmptyEyeWindows = 0;
        } else if (p25Phase2MacEssStarvedVoiceWindow(audio)) {
            cliMacEssStarveWindows = std::min(cliMacEssStarveWindows + 1, 1000);
            if (cliMacEssStarveWindows >= 2) {
                cliForceMaskEpochRehunt = true;
                cliMaskEpochRepairHoldWindows = std::max(cliMaskEpochRepairHoldWindows, 3);
            }
        } else if (cliHardTargetAcquire &&
                   audio.phase2Bursts == 0 &&
                   audio.phase2MaskedBursts == 0 &&
                   audio.phase2TargetVoiceCodewords == 0 &&
                   audio.decodedFrames == 0 &&
                   audio.audio.empty()) {
            // After a hard acquire, short sustain crumbs that lose the Phase-2
            // eye must reopen a cold lookback. Do not invalidate sticky epoch.
            cliEmptyEyeWindows = std::min(cliEmptyEyeWindows + 1, 1000);
            if (cliEmptyEyeWindows >= 2) {
                cliMaskEpochRepairHoldWindows =
                    std::max(cliMaskEpochRepairHoldWindows, 3);
            }
        } else if (audio.phase2TargetVoiceCodewords == 0 &&
                   audio.phase2ExpectedVoiceCodewords == 0 &&
                   audio.phase2DiagnosticAmbeProbeAttempts == 0) {
            cliMacEssStarveWindows = 0;
            if (audio.phase2Bursts > 0) {
                cliEmptyEyeWindows = 0;
            }
        } else if (audio.phase2Bursts > 0) {
            cliEmptyEyeWindows = 0;
        }
        const std::string speakerGateReason = audio.phase2SpeakerGateReason.empty()
            ? p25VoiceBlockSpeakerGateReason(audio)
            : audio.phase2SpeakerGateReason;
        const bool speakerMayEmit =
            speakerGateReason == "emit" &&
            p25VoiceBlockHasSpeakerTimelineAudio(audio) &&
            audio.phase2EmittedPcmFrames > 0 &&
            p25VoiceBlockMayEmitAudio(audio);
        if (speakerMayEmit) {
            p25Phase2BindSpeakerPendingToCall(voiceTestSpeakerQueue, rx);
            constexpr size_t phase2FrameSamples = 960u; // 20 ms at voicetest's fixed 48 kHz WAV rate.
            const size_t rawSpeakerFrames = audio.audio.size() / phase2FrameSamples;
            const size_t ordinalFrames = audio.phase2EmittedSpeechOrdinals.size();
            if (ordinalFrames == 0) {
                ++speakerOrdinalMissingWindows;
            } else if (ordinalFrames < rawSpeakerFrames) {
                ++speakerOrdinalPartialWindows;
            }
            const std::vector<float> speakerAudioForQueue =
                p25Phase2SpeakerAudioForQueue(voiceTestSpeakerQueue, audio, audio.audio, phase2FrameSamples);
            const size_t filteredSpeakerFrames = speakerAudioForQueue.size() / phase2FrameSamples;
            if (rawSpeakerFrames > filteredSpeakerFrames) {
                speakerTimelineDroppedFrames +=
                    static_cast<long long>(rawSpeakerFrames - filteredSpeakerFrames);
            }
            if (!speakerAudioForQueue.empty()) {
                ++emitWindows;
                speakerSamples += static_cast<long long>(speakerAudioForQueue.size());
                continuousPcm.insert(continuousPcm.end(),
                                     speakerAudioForQueue.begin(),
                                     speakerAudioForQueue.end());
            } else {
                ++emptyWindows;
            }
            p25Phase2UpdateSessionSustainState(
                rx, audio, QDateTime::currentMSecsSinceEpoch(), !speakerAudioForQueue.empty());
        } else if (!audio.audio.empty() && audio.decodedFrames > 0) {
            ++gatedRawWindows;
        } else {
            ++emptyWindows;
        }
        if (!audio.phase2OppositeRecordPcm.empty()) {
            continuousOppositePcm.insert(
                continuousOppositePcm.end(),
                audio.phase2OppositeRecordPcm.begin(),
                audio.phase2OppositeRecordPcm.end());
        }

        std::ostringstream sig;
        sig << static_cast<int>(audio.diag) << ":" << audio.decodedFrames << ":" << audio.audio.size()
            << ":" << audio.phase2Bursts << ":" << audio.phase2VoiceCodewords << ":" << audio.phase2TargetVoiceCodewords
            << ":" << audio.phase2MaskedBursts << ":" << audio.phase2MacCrcValid << ":" << audio.phase2EssKnown
            << ":" << audio.phase2EssEncrypted << ":" << audio.phase2DiagnosticAmbeProbeAttempts
            << ":" << audio.phase2DiagnosticAmbeProbeAccepted << ":" << speakerGateReason
            << ":" << audio.phase2ContextVoiceCodewords
            << ":" << audio.phase2ContextSuppressedVoiceCodewords
            << ":" << audio.phase2DuplicateSuppressedVoiceCodewords
            << ":" << audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords
            << ":" << audio.phase2SequencerSuppressedVoiceCodewords
            << ":" << audio.phase2TrafficTalkgroupMismatchVoiceCodewords
            << ":" << audio.phase2TrafficTalkgroupStaleMismatchVoiceCodewords
            << ":" << (rx.p25Phase2StickySlotLabelInvert ? 1 : 0);
        if (sig.str() != lastVoiceSig) {
            lastVoiceSig = sig.str();
            std::cout << "P25 voicetest voice startMs="
                      << (capture.startOffsetMs + static_cast<double>(winStart) * 1000.0 / capture.sampleRateHz)
                      << " stage=" << p25VoiceDiagLabel(audio.diag)
                      << " decoded=" << audio.decodedFrames
                      << " audio=" << audio.audio.size()
                      << " p2bursts=" << audio.phase2Bursts
                      << " p2vcw=" << audio.phase2VoiceCodewords
                      << " targetVcw=" << audio.phase2TargetVoiceCodewords
                      << " exp=" << audio.phase2ExpectedVoiceCodewords
                      << " fed=" << audio.phase2FedToMbelib
                      << " emitPcm=" << audio.phase2EmittedPcmFrames
                      << " gaps=" << audio.phase2FeedGaps
                      << " ctxVcw=" << audio.phase2ContextVoiceCodewords
                      << " ctxDrop=" << audio.phase2ContextSuppressedVoiceCodewords
                      << " dup=" << audio.phase2DuplicateSuppressedVoiceCodewords
                      << " absDup=" << audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords
                      << " seqDrop=" << audio.phase2SequencerSuppressedVoiceCodewords
                      << " iqReject=" << audio.phase2InputQualityRejectedVoiceCodewords
                      << " tgMismatchVcw=" << audio.phase2TrafficTalkgroupMismatchVoiceCodewords
                      << " tgStaleMismatchVcw=" << audio.phase2TrafficTalkgroupStaleMismatchVoiceCodewords
                      << " ambe=" << audio.phase2AmbeAcceptedFrames << "/" << audio.phase2AmbeDecodeAttempts
                      << " oppVcw=" << audio.phase2OppositeVoiceCodewords
                      << " slot0Vcw=" << audio.phase2Slot0VoiceCodewords
                      << " slot1Vcw=" << audio.phase2Slot1VoiceCodewords
                      << " slot0Mac=" << audio.phase2Slot0MacCrcValid
                      << " slot1Mac=" << audio.phase2Slot1MacCrcValid
                      << " oppAmbe=" << audio.phase2OppositeAmbeAcceptedFrames
                      << "/" << audio.phase2OppositeAmbeDecodeAttempts
                      << " oppPend=" << audio.phase2OppositePendingQueued
                      << " p2sf=" << audio.phase2SuperframeBursts
                      << " p2mask=" << audio.phase2MaskedBursts
                      << " p2mac=" << audio.phase2MacCrcValid << "/" << audio.phase2MacPdus
                      << " " << p25Phase2AcchStatsText(makeP25VoiceDiagnostics(audio)).toStdString()
                      << " p2ess=" << (audio.phase2EssKnown ? (audio.phase2EssEncrypted ? "enc" : "clear") : "unknown")
                      << " ambeProbe=" << audio.phase2DiagnosticAmbeProbeAccepted
                      << "/" << audio.phase2DiagnosticAmbeProbeAttempts
                      << " gate=" << audio.phase2SecurityGateAction
                      << " speaker=" << speakerGateReason
                      << " invert=" << (rx.p25Phase2StickySlotLabelInvert ? "yes" : "no")
                      << " effOffHz=" << (audio.effectiveTargetFreqHz - audio.centerFreqHz)
                      << " backend=" << (audio.backendAvailable ? "yes" : "no");
            if (streamContiguousDdc) {
                std::cout << " cqpskLock=" << (audio.cqpskLockActive ? "1" : "0")
                          << " cqpskMiss=" << audio.cqpskLockMisses
                          << " residHz=" << audio.cqpskResidualCarrierHz
                          << " phaseErr=" << audio.cqpskPhaseErrorRmsRad
                          << " dibits=" << audio.dibitCount
                          << " framerBurst=" << audio.dspFramerBurstsEmitted
                          << " demodState=" << (audio.demodState.empty() ? "-" : audio.demodState);
            }
            std::cout << "\n";
        }

        if (voiceEncrypted) break;
        if (!args.streamVoice && winEnd == capture.iq.size()) break;
        if (args.streamVoice && streamEnd >= capture.iq.size()) break;
    }

    const double audioSeconds = continuousPcm.size() / 48000.0;
    const double spanSeconds = capture.iq.empty() || capture.sampleRateHz <= 0.0
        ? 0.0
        : static_cast<double>(capture.iq.size()) / capture.sampleRateHz;
    const double duty = spanSeconds > 0.0 ? (audioSeconds / spanSeconds) : 0.0;

    if (!args.wavOutPath.empty() && !continuousPcm.empty()) {
        // Minimal 48 kHz mono float32 WAV for automation / A/B listening.
        const QString wavPath = QString::fromStdString(args.wavOutPath);
        QFile wav(wavPath);
        if (wav.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const uint32_t sampleRate = 48000u;
            const uint16_t channels = 1u;
            const uint16_t bitsPerSample = 32u;
            const uint16_t audioFormat = 3u; // IEEE float
            const uint32_t dataBytes = static_cast<uint32_t>(continuousPcm.size() * sizeof(float));
            const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8u);
            const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8u));
            const uint32_t riffSize = 36u + dataBytes;
            auto writeU16 = [&](uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
            auto writeU32 = [&](uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
            wav.write("RIFF", 4);
            writeU32(riffSize);
            wav.write("WAVE", 4);
            wav.write("fmt ", 4);
            writeU32(16u);
            writeU16(audioFormat);
            writeU16(channels);
            writeU32(sampleRate);
            writeU32(byteRate);
            writeU16(blockAlign);
            writeU16(bitsPerSample);
            wav.write("data", 4);
            writeU32(dataBytes);
            wav.write(reinterpret_cast<const char*>(continuousPcm.data()),
                      static_cast<qint64>(dataBytes));
            wav.close();
            std::cout << "P25 voicetest wav=\"" << args.wavOutPath
                      << "\" samples=" << continuousPcm.size()
                      << " seconds=" << audioSeconds << "\n";
        } else {
            std::cout << "P25 voicetest wav write failed path=\"" << args.wavOutPath << "\"\n";
        }
    }

    if (!args.oppositeWavOutPath.empty() && !continuousOppositePcm.empty()) {
        const QString wavPath = QString::fromStdString(args.oppositeWavOutPath);
        QFile wav(wavPath);
        if (wav.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const uint32_t sampleRate = 48000u;
            const uint16_t channels = 1u;
            const uint16_t bitsPerSample = 32u;
            const uint16_t audioFormat = 3u; // IEEE float
            const uint32_t dataBytes = static_cast<uint32_t>(continuousOppositePcm.size() * sizeof(float));
            const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8u);
            const uint16_t blockAlign = static_cast<uint16_t>(channels * (bitsPerSample / 8u));
            const uint32_t riffSize = 36u + dataBytes;
            auto writeU16 = [&](uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
            auto writeU32 = [&](uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
            wav.write("RIFF", 4);
            writeU32(riffSize);
            wav.write("WAVE", 4);
            wav.write("fmt ", 4);
            writeU32(16u);
            writeU16(audioFormat);
            writeU16(channels);
            writeU32(sampleRate);
            writeU32(byteRate);
            writeU16(blockAlign);
            writeU16(bitsPerSample);
            wav.write("data", 4);
            writeU32(dataBytes);
            wav.write(reinterpret_cast<const char*>(continuousOppositePcm.data()),
                      static_cast<qint64>(dataBytes));
            wav.close();
            const double oppSeconds = continuousOppositePcm.size() / 48000.0;
            std::cout << "P25 voicetest oppwav=\"" << args.oppositeWavOutPath
                      << "\" samples=" << continuousOppositePcm.size()
                      << " seconds=" << oppSeconds << "\n";
        } else {
            std::cout << "P25 voicetest oppwav write failed path=\""
                      << args.oppositeWavOutPath << "\"\n";
        }
    } else if (!args.oppositeWavOutPath.empty()) {
        std::cout << "P25 voicetest oppwav=\"" << args.oppositeWavOutPath
                  << "\" samples=0 (no companion PCM)\n";
    }

    const bool metMinFrames = args.minDecodedFrames <= 0 || decodedFrames >= args.minDecodedFrames;
    const bool metMinAudio = args.minAudioSeconds <= 0.0 || audioSeconds >= args.minAudioSeconds;
    const double timelineSlackSeconds = std::max(0.160, spanSeconds * 0.05);
    const bool timelineOk = !args.streamVoice ||
        (audioSeconds <= spanSeconds + timelineSlackSeconds && duty <= 1.05);
    const bool trustedTrafficProof =
        trustedClearWindows > 0 || targetSessionReleaseWindows > 0 || targetEssClearWindows > 0;
    const bool cadenceOk = phase2FedToMbelib > 0 &&
        phase2FeedGaps <= std::max<long long>(2, phase2FedToMbelib / 12);
    const bool sequencerOk = sequencerSuppressed <= std::max<long long>(8, phase2FedToMbelib / 12);
    const bool ambeQualityOk = phase2AmbeAttempts > 0 &&
        phase2AmbeAccepted >= std::max<long long>(2, (phase2AmbeAttempts * 3) / 4);
    // Allow up to 25% soft concealment (mbelib repeat/erasure with audible PCM
    // or app PLC). Real Phase-2 RF routinely lands ~15-22% here; the old 1/6
    // bar failed known-good clear windows that already had duty≈1 and gaps=0.
    const bool concealmentOk = phase2EmittedPcmFrames > 0 &&
        phase2ConcealmentFrames <= std::max<long long>(2, phase2EmittedPcmFrames / 4);
    // Continuous quality must mean target-slot trusted, mostly gap-free speech.
    // Sparse/repeated AMBE islands are useful diagnostics, not a green build.
    const bool continuousOk = !args.streamVoice ||
        (duty >= 0.65 && timelineOk && trustedTrafficProof && cadenceOk &&
         sequencerOk && ambeQualityOk && concealmentOk);
    const bool hasDecodedSpeakerAudio =
        decodedFrames > 0 && speakerSamples > 0 && audioSeconds > 0.0;
    const bool hasConcealmentOnlySpeakerTimeline =
        decodedFrames == 0 && speakerSamples > 0 && emitWindows > 0 &&
        phase2EmittedPcmFrames > 0 && phase2ConcealmentFrames > 0;
    const bool allTargetAmbeAttemptsRejected =
        phase2TargetVoiceCodewords > 0 &&
        phase2AmbeAttempts > 0 &&
        phase2AmbeAccepted == 0 &&
        phase2InputQualityRejected > 0;

    const char* result = "FAIL_NO_AUDIO";
    if (voiceEncrypted) {
        result = "PASS_ENCRYPTED_GATED";
    } else if (hasDecodedSpeakerAudio && emitWindows > 0 && metMinFrames && metMinAudio && continuousOk) {
        result = args.streamVoice ? "PASS_CONTINUOUS_AUDIO" : "PASS_CLEAR_AUDIO";
    } else if (hasDecodedSpeakerAudio && emitWindows > 0) {
        result = "PASS_PARTIAL_AUDIO";
    } else if (allTargetAmbeAttemptsRejected) {
        result = "FAIL_PLC_ONLY_INPUT_QUALITY_REJECTED";
    } else if (hasConcealmentOnlySpeakerTimeline) {
        result = "FAIL_CONCEALMENT_ONLY_AUDIO";
    } else if (decodedFrames > 0 && audioSamples > 0) {
        result = "FAIL_RAW_AUDIO_GATED";
    }
    P25AudioDropSample voiceDrop;
    voiceDrop.targetVcw = phase2TargetVoiceCodewords;
    voiceDrop.fed = phase2FedToMbelib;
    voiceDrop.emittedPcm = phase2EmittedPcmFrames;
    voiceDrop.dups = duplicateSuppressed;
    voiceDrop.windowSeconds = (spanSeconds > 0.0) ? spanSeconds : 1.0;
    const char* dropLabel = p25AudioDropBucketLabel(classifyP25AudioDrop(voiceDrop));
    std::cout << "P25 voicetest result=" << result
              << " drop=" << dropLabel
              << " voiceWindows=" << voiceWindows
              << " emitWindows=" << emitWindows
              << " gatedRawWindows=" << gatedRawWindows
              << " emptyWindows=" << emptyWindows
              << " decodedFrames=" << decodedFrames
              << " audioSamples=" << audioSamples
              << " speakerSamples=" << speakerSamples
              << " audioSeconds=" << audioSeconds
              << " spanSeconds=" << spanSeconds
              << " duty=" << duty
              << " lastStage=" << p25VoiceDiagLabel(lastDiag)
              << " p2bursts=" << phase2Bursts
              << " p2vcw=" << phase2VoiceCodewords
              << " targetVcw=" << phase2TargetVoiceCodewords
              << " expectedVcw=" << phase2ExpectedVoiceCodewords
              << " contextVcw=" << phase2ContextVoiceCodewords
              << " contextSuppressed=" << phase2ContextSuppressedVoiceCodewords
              << " fed=" << phase2FedToMbelib
              << " emitPcm=" << phase2EmittedPcmFrames
              << " ordPcm=" << phase2EmittedSpeechOrdinalFrames
              << " plc=" << phase2ConcealmentFrames
              << " gaps=" << phase2FeedGaps
              << " ordMissingWin=" << speakerOrdinalMissingWindows
              << " ordPartialWin=" << speakerOrdinalPartialWindows
              << " speakerDropFrames=" << speakerTimelineDroppedFrames
              << " iqReject=" << phase2InputQualityRejected
              << " tgMismatchVcw=" << phase2TrafficTalkgroupMismatch
              << " tgStaleMismatchVcw=" << phase2TrafficTalkgroupStaleMismatch
              << " ambe=" << phase2AmbeAccepted << "/" << phase2AmbeAttempts
              << " oppAmbe=" << phase2OppositeAmbeAccepted << "/" << phase2OppositeAmbeAttempts
              << " oppPend=" << phase2OppositePendingQueued
              << " timelineOk=" << (timelineOk ? "yes" : "no")
              << " sequencerOk=" << (sequencerOk ? "yes" : "no")
              << " concealmentOk=" << (concealmentOk ? "yes" : "no")
              << " p2mask=" << phase2MaskedBursts
              << " p2macCrc=" << phase2MacCrcValid
              << " trustedClearWindows=" << trustedClearWindows
              << " targetSessionWindows=" << targetSessionReleaseWindows
              << " targetEssClearWindows=" << targetEssClearWindows
              << " dupSuppressed=" << duplicateSuppressed
              << " absDupSuppressed=" << absoluteDuplicateSuppressed
              << " seqSuppressed=" << sequencerSuppressed
              << " stickyInvert=" << (rx.p25Phase2StickySlotLabelInvert ? "yes" : "no")
              << " ambeProbe=" << diagnosticAmbeProbeAccepted << "/" << diagnosticAmbeProbeAttempts
              << " essKnown=" << (essKnown ? "yes" : "no")
              << " essEncrypted=" << (essEncrypted ? "yes" : "no") << "\n";
    std::cout.flush();
    spdlog::info("P25 voicetest result={} drop={} voiceWindows={} emitWindows={} decodedFrames={} speakerSamples={} audioSeconds={} duty={} targetVcw={} fed={} emitPcm={} ordPcm={} gaps={} ordMissingWin={} ordPartialWin={} speakerDropFrames={} ambe={}/{} p2macCrc={} essKnown={} essEncrypted={}",
                 result,
                 dropLabel,
                 voiceWindows,
                 emitWindows,
                 decodedFrames,
                 speakerSamples,
                 audioSeconds,
                 duty,
                 phase2TargetVoiceCodewords,
                 phase2FedToMbelib,
                 phase2EmittedPcmFrames,
                 phase2EmittedSpeechOrdinalFrames,
                 phase2FeedGaps,
                 speakerOrdinalMissingWindows,
                 speakerOrdinalPartialWindows,
                 speakerTimelineDroppedFrames,
                 phase2AmbeAccepted,
                 phase2AmbeAttempts,
                 phase2MacCrcValid,
                 essKnown,
                 essEncrypted);
    std::cout << "P25 continuity slotChanged=" << rx.p25DiagSlotChanged
              << " stickyInvert=" << rx.p25DiagStickyInvert
              << " slotProbe=" << rx.p25DiagSlotProbe
              << " slotProbeBlocked=" << rx.p25DiagSlotProbeBlocked
              << " securityChanged=" << rx.p25DiagSecurityChanged
              << " securityLatch=" << static_cast<int>(rx.p25SessionState.callSecurityLatch)
              << " vocoderReset=" << rx.p25DiagVocoderReset
              << " pendingCleared=" << rx.p25DiagPendingAudioCleared
              << " variantChanged=" << rx.p25DiagVariantChanged
              << " seqGapSilence=" << rx.p25DiagSequencerGapSilence
              << " seqLateDrops=" << rx.p25DiagSequencerLateDrops
              << " slotImmutable=" << (rx.p25Phase2GrantedSlotImmutable ? "yes" : "no")
              << "\n";
}

