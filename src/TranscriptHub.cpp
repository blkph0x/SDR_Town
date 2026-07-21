#include "TranscriptHub.h"

TranscriptHub::TranscriptHub(QObject* parent)
    : QObject(parent)
{
}

void TranscriptHub::registerSource(const TranscriptSourceInfo& info)
{
    if (info.id.isEmpty()) return;
    {
        QMutexLocker lock(&m_mutex);
        for (auto& existing : m_sources) {
            if (existing.id == info.id) {
                existing = info;
                lock.unlock();
                emit sourcesChanged();
                return;
            }
        }
        m_sources.push_back(info);
    }
    emit sourcesChanged();
}

QVector<TranscriptSourceInfo> TranscriptHub::sources() const
{
    QMutexLocker lock(&m_mutex);
    return m_sources;
}

void TranscriptHub::append(TranscriptMessage msg)
{
    if (!msg.timestamp.isValid()) {
        msg.timestamp = QDateTime::currentDateTime();
    }
    {
        QMutexLocker lock(&m_mutex);
        m_messages.push_back(msg);
        while (m_messages.size() > kMaxMessages) {
            m_messages.removeFirst();
        }
    }
    emit messageAppended(msg);
}

void TranscriptHub::appendSystem(const QString& text)
{
    TranscriptMessage msg;
    msg.timestamp = QDateTime::currentDateTime();
    msg.sourceId = QStringLiteral("system");
    msg.sourceName = QStringLiteral("System");
    msg.category = TranscriptCategory::System;
    msg.text = text;
    append(std::move(msg));
}

QVector<TranscriptMessage> TranscriptHub::messages() const
{
    QMutexLocker lock(&m_mutex);
    return m_messages;
}

void TranscriptHub::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_messages.clear();
    }
    emit cleared();
}

int TranscriptHub::messageCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_messages.size();
}
