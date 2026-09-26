#pragma once
#include "FmDiagnostics.h"
#include <QJsonObject>
#include <QString>
#include <QLockFile>
#include <memory>
class QObject;

class FmDiagnosticsLog {
public:
    explicit FmDiagnosticsLog(QString directory = {});
    QJsonObject sample(); // Caller runs off the DSP thread; empty if idle.
    QString path() const { return path_; }
private:
    QString path_;
    std::unique_ptr<QLockFile> lock_;
    std::array<uint64_t,2> lastBlocks_{};
};
void startFmDiagnostics(QObject* parent);
