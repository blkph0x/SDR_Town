#include "InmarsatDiagnostics.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <mutex>
#include <stdexcept>

namespace {
std::mutex sinkMutex;
InmarsatDiagnosticSink sink;
}
void setInmarsatDiagnosticSink(InmarsatDiagnosticSink next) {
    std::lock_guard<std::mutex> guard(sinkMutex);
    sink = std::move(next);
}
void InmarsatDiagnostics::open(const QString& directory, const QString& source, bool remoteAllowed) {
    file_.close();
    source_ = source;
    remoteAllowed_ = remoteAllowed;
    session_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    error_.clear();
    sequence_ = suppressed_ = 0;
    lastRemote_ = {};
    const QString dir = directory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/inmarsat_diagnostics" : directory;
    if (!QDir().mkpath(dir)) throw std::runtime_error("Cannot create Inmarsat diagnostic directory");
    file_.setFileName(QDir(dir).filePath("inmarsat_" + session_ + ".jsonl"));
    if (!file_.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw std::runtime_error("Cannot create Inmarsat diagnostic report");
}
nlohmann::json InmarsatDiagnostics::remotePayload(const nlohmann::json& details) {
    // DEC-0120: construct an allowlist; do not rely on a blacklist of personal fields.
    nlohmann::json result = nlohmann::json::object();
    if (details.contains("samples") && details["samples"].is_number_unsigned())
        result["processedSampleCount"] = details["samples"];
    for (const char* key : {"blocks", "resets", "discontinuities", "rateHz", "mode", "symbols",
         "rawBlocks", "carrierDetected", "quality", "processingMs", "maxBlockMs", "peakComponent", "rms",
         "protocolLock", "validatedFrames", "voiceFrames", "pcmSamples", "protocolDecoderAvailable",
         "aeroVocoderAvailable", "state", "errorCode", "positionSamples", "totalSamples", "realTime"}) {
        if (details.contains(key) && details[key].is_primitive()) result[key] = details[key];
    }
    for(const char* key:{"crcFailed","rejectedCFrames","codecCorrections","codecRepeats","codecMutes","softBits","speechFrames"})
        if(details.contains(key) && details[key].is_number()) result[key]=details[key];
    if(details.contains("watch") && details["watch"].is_object()) {
        const auto& watch=details["watch"];
        for(const char* key:{"group","groups","visitPositions","positionTarget","switches","iqGaps","groupSeconds",
            "processingSeconds","inputSeconds","loadRatio","maxBlockMs"})
            if(watch.contains(key) && watch[key].is_number())result[std::string("watch_")+key]=watch[key];
        if(watch.contains("refreshDue") && watch["refreshDue"].is_boolean())result["watch_refreshDue"]=watch["refreshDue"];
        // Never transmit watch frequencies, channel labels, AES IDs or decoded positions.
    }
    if(details.contains("audio") && details["audio"].is_object())
        for(const char* key:{"speakerQueued","speakerDropped","speakerZeroFill","speakerConsumed",
                            "pcmReceived","pcmNonzero","pcmPeak","pcmRms","wavSamples"})
            if(details["audio"].contains(key) && details["audio"][key].is_number())result[key]=details["audio"][key];
    if(details.contains("audio") && details["audio"].is_object())
        for(const char* key:{"speakerRequested","speakerRunning","speakerFailed"})
            if(details["audio"].contains(key) && details["audio"][key].is_boolean())result[key]=details["audio"][key];
    return result;
}
void InmarsatDiagnostics::write(const char* event, const nlohmann::json& details, bool final) {
    const auto timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    nlohmann::json row = {{"schema", "sdr-town-inmarsat-v1"}, {"session", session_.toStdString()},
        {"source", source_.toStdString()}, {"event", event}, {"sequence", ++sequence_},
        {"utc", timestamp}, {"suppressedLocalRecords", suppressed_}, {"detail", details}};
    // Leave room for a final summary after the progress cap. No IQ/PCM is logged.
    if (final || file_.size() < 8 * 1024 * 1024) {
        const auto line = QByteArray::fromStdString(row.dump()) + '\n';
        if (file_.write(line) != line.size() || !file_.flush()) error_ = "Diagnostic file write failed";
    } else ++suppressed_;
    const auto now = std::chrono::steady_clock::now();
    // Short automated replays need only their final summary. An extra startup
    // message can consume the existing transport's bounded shutdown drain budget.
    if (remoteAllowed_ && std::string(event) != "open" && (final || lastRemote_ == std::chrono::steady_clock::time_point{} ||
        now - lastRemote_ >= std::chrono::seconds(5))) {
        InmarsatDiagnosticSink callback;
        { std::lock_guard<std::mutex> guard(sinkMutex); callback = sink; }
        if (callback) {
            auto payload = remotePayload(details);
            payload["session"] = session_.toStdString();
            payload["source"] = source_.toStdString();
            payload["event"] = event;
            try { callback(payload); }
            catch (...) { error_ = "Remote diagnostic submission failed"; }
        }
        lastRemote_ = now;
    }
}
