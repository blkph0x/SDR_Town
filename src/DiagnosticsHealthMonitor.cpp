#include "DiagnosticsHealthMonitor.h"
#include <QTimer>
#include <algorithm>

DiagnosticsHealthMonitor::DiagnosticsHealthMonitor(QObject* owner,
    std::function<QString()> session, std::function<void()> started,
    std::function<void(qint64)> stalled, std::function<bool()> resourcePressure)
    : QObject(owner), session_(std::move(session)), started_(std::move(started)),
      stalled_(std::move(stalled)), resourcePressure_(std::move(resourcePressure)) {
    clock_.start();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { poll(clock_.elapsed()); });
    timer->start(1000);
}

void DiagnosticsHealthMonitor::poll(qint64 now) {
    const auto session = session_();
    if (session.isEmpty() || session != currentSession_) {
        currentSession_ = session;
        lastBeat_ = lastResourceCheck_ = now;
        lastStall_ = now - 60000;
        lastPressure_ = now - 300000;
        if (!session.isEmpty()) started_();
        return;
    }
    const qint64 drift = now - lastBeat_;
    lastBeat_ = now;
    // Retain existing reporting thresholds; these never govern DSP or audio.
    if (drift >= 8000 && now - lastStall_ >= 60000) {
        lastStall_ = now;
        stalled_(std::min<qint64>(drift, 600000));
    }
    if (now - lastResourceCheck_ >= 60000) {
        lastResourceCheck_ = now;
        if (now - lastPressure_ >= 300000 && resourcePressure_()) lastPressure_ = now;
    }
}
