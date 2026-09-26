#include "InmarsatDiagnostics.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <mutex>
#include <stdexcept>
#include <cmath>

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
    // Never accept strings/objects in numeric slots; even a known field name
    // is not permission to upload arbitrary decoder text. DEC-0139.
    const auto numbers=[](const nlohmann::json& input,nlohmann::json& output) {
        for(const char* key:{"validationMs","setupMs","probeMs","channelizerMs","modemMs",
            "inputSeconds","loadRatio","lastBlockMs","lastInputMs","overBudgetBlocks","messages"})
            if(input.contains(key) && input[key].is_number() && std::isfinite(input[key].get<double>()))output[key]=input[key];
    };
    numbers(details,result);
    if (details.contains("samples") && details["samples"].is_number_unsigned())
        result["processedSampleCount"] = details["samples"];
    for (const char* key : {"blocks", "resets", "discontinuities", "rateHz", "mode", "symbols",
         "rawBlocks", "carrierDetected", "quality", "processingMs", "maxBlockMs", "peakComponent", "rms",
         "protocolLock", "validatedFrames", "voiceFrames", "pcmSamples", "protocolDecoderAvailable",
         "aeroVocoderAvailable", "state", "errorCode", "positionSamples", "totalSamples", "realTime"}) {
        if (!details.contains(key))continue;
        const auto& value=details[key];
        const std::string field=key;
        const bool boolField=field=="carrierDetected" || field=="protocolLock" ||
            field=="protocolDecoderAvailable" || field=="aeroVocoderAvailable" || field=="realTime";
        if (boolField ? value.is_boolean() : (value.is_number() && std::isfinite(value.get<double>()))) result[key]=value;
        else if(value.is_string()) {
            const auto text=value.get<std::string>();
            if((field=="state" && (text=="idle"||text=="opening"||text=="playing"||text=="paused"||text=="running"||text=="stopped"||text=="complete"||text=="error"||text=="hardware_lost")) ||
               (field=="errorCode" && (text=="none"||text=="replay_failed"||text=="invalid_input"||text=="hardware_lost")) ||
               (field=="mode" && (text=="aero_msk"||text=="aero_oqpsk"||text=="aero_voice"||text=="aero_burst"||text=="egc")))result[key]=value;
        }
    }
    for(const char* key:{"crcFailed","rejectedCFrames","codecCorrections","codecRepeats","codecMutes","softBits","speechFrames"})
        if(details.contains(key) && details[key].is_number() && std::isfinite(details[key].get<double>())) result[key]=details[key];
    if(details.contains("watch") && details["watch"].is_object()) {
        const auto& watch=details["watch"];
        for(const char* key:{"group","groups","visitPositions","positionTarget","switches","iqGaps","groupSeconds",
            "processingSeconds","inputSeconds","loadRatio","maxBlockMs","workerCount","maxPendingBlocksPerChannel"})
            if(watch.contains(key) && watch[key].is_number() && std::isfinite(watch[key].get<double>()))result[std::string("watch_")+key]=watch[key];
        if(watch.contains("refreshDue") && watch["refreshDue"].is_boolean())result["watch_refreshDue"]=watch["refreshDue"];
        // Never transmit watch frequencies, channel labels, AES IDs or decoded positions.
        if(watch.contains("channels") && watch["channels"].is_array()) {
            result["workers"]=nlohmann::json::array();
            for(const auto& ch:watch["channels"]) {
                if(result["workers"].size()==16)break; // Matches the tested worker budget.
                if(!ch.is_object() || !ch.contains("decoder") || !ch["decoder"].is_object())continue;
                const auto& decoder=ch["decoder"];
                nlohmann::json worker=nlohmann::json::object();numbers(decoder,worker);
                for(const char* key:{"blocks","resets","discontinuities","rateHz","processingMs","maxBlockMs",
                    "validatedFrames","crcFailed","voiceFrames","speechFrames","pcmSamples","codecCorrections","codecMutes"})
                    if(decoder.contains(key) && decoder[key].is_number() && std::isfinite(decoder[key].get<double>()))worker[key]=decoder[key];
                if(decoder.contains("protocolLock") && decoder["protocolLock"].is_boolean())worker["protocolLock"]=decoder["protocolLock"];
                if(ch.contains("rate") && ch["rate"].is_number_integer())worker["bitRate"]=ch["rate"];
                worker["index"]=result["workers"].size();
                result["workers"].push_back(std::move(worker));
            }
        }
    }
    if(details.contains("audio") && details["audio"].is_object())
        for(const char* key:{"speakerQueued","speakerDropped","speakerZeroFill","speakerConsumed",
                            "pcmReceived","pcmNonzero","pcmPeak","pcmRms","wavSamples"})
            if(details["audio"].contains(key) && details["audio"][key].is_number() && std::isfinite(details["audio"][key].get<double>()))result[key]=details["audio"][key];
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
