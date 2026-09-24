#pragma once
#include <QFile>
#include <QString>
#include <nlohmann/json.hpp>
#include <functional>
#include <chrono>

using InmarsatDiagnosticSink = std::function<void(const nlohmann::json&)>;
void setInmarsatDiagnosticSink(InmarsatDiagnosticSink sink);

// Worker-owned, bounded local report. Transport is injected by app, not decoder.
class InmarsatDiagnostics {
public:
    void open(const QString& directory, const QString& source, bool remoteAllowed = true);
    void write(const char* event, const nlohmann::json& details, bool final = false);
    QString path() const { return file_.fileName(); }
    QString error() const { return error_; }
    QString session() const { return session_; }
    static nlohmann::json remotePayload(const nlohmann::json& details);
private:
    QFile file_;
    QString session_, source_, error_;
    uint64_t sequence_ = 0, suppressed_ = 0;
    bool remoteAllowed_ = false;
    std::chrono::steady_clock::time_point lastRemote_{};
};

// Defined only in the application adapter; preserves configured opt-in and budget.
void connectInmarsatRemoteDiagnostics();
class InmarsatReplayDialog;
void configureInmarsatReplaySharing(InmarsatReplayDialog& dialog);
