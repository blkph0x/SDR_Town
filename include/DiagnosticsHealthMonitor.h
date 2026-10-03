#pragma once
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <functional>

// DEC-0171: GUI-owned scheduler. No RF content; callbacks never run opted out.
// poll's time is monotonic milliseconds, injectable for deterministic tests.
class DiagnosticsHealthMonitor : public QObject {
public:
    DiagnosticsHealthMonitor(QObject* owner, std::function<QString()> session,
        std::function<void()> started, std::function<void(qint64)> stalled,
        std::function<bool()> resourcePressure);
    void poll(qint64 now);
private:
    std::function<QString()> session_;
    std::function<void()> started_;
    std::function<void(qint64)> stalled_;
    std::function<bool()> resourcePressure_;
    QElapsedTimer clock_;
    QString currentSession_;
    qint64 lastBeat_ = 0, lastStall_ = -60000;
    qint64 lastResourceCheck_ = 0, lastPressure_ = -300000;
};
