#pragma once

#include "TranscriptTypes.h"

#include <QObject>
#include <QMutex>
#include <QVector>

// Central chronological message bus for all decode / STT feeds.
// Sources call append(); TranscriptWindow observes signals.
class TranscriptHub : public QObject
{
    Q_OBJECT
public:
    explicit TranscriptHub(QObject* parent = nullptr);

    void registerSource(const TranscriptSourceInfo& info);
    QVector<TranscriptSourceInfo> sources() const;

    void append(TranscriptMessage msg);
    void appendSystem(const QString& text);

    QVector<TranscriptMessage> messages() const;
    void clear();
    int messageCount() const;

    static constexpr int kMaxMessages = 5000;

signals:
    void messageAppended(const TranscriptMessage& msg);
    void cleared();
    void sourcesChanged();

private:
    mutable QMutex m_mutex;
    QVector<TranscriptSourceInfo> m_sources;
    QVector<TranscriptMessage> m_messages;
};
