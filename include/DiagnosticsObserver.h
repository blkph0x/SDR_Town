#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <functional>
#include <chrono>

// Read-only GUI-thread observer. DEC-0160: no raw text, settings dump or RF work.
class DiagnosticsObserver : public QObject {
public:
    using Snapshot = std::function<QJsonObject()>;
    using Submit = std::function<void(const QString&, const QJsonObject&)>;
    DiagnosticsObserver(QObject* owner, Snapshot snapshot,
        std::function<QString()> session, Submit submit);
    void tick();
    void observe(QObject* root);
    void record(QObject* object, const QString& kind, QJsonValue value = {});
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    Snapshot snapshot_;
    std::function<QString()> session_;
    Submit submit_;
    QSet<QObject*> wired_;
    QJsonArray actions_;
    QString currentSession_;
    int ticks_=0, dropped_=0;
    quint64 batch_=0;
    std::chrono::steady_clock::time_point lastIntent_{};
};
