#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>

// Unified decode / transcript message used by the Decode Log window.
// New sources (AM STT, ADS-B, ACARS, POCSAG, …) publish the same shape;
// the window only filters/displays — it does not know source internals.

enum class TranscriptCategory {
    VoiceStt = 0,
    Adsb,
    Acars,
    Pocsag,
    System,
    Other
};

inline QString transcriptCategoryDisplayName(TranscriptCategory cat)
{
    switch (cat) {
    case TranscriptCategory::VoiceStt: return QStringLiteral("Voice STT");
    case TranscriptCategory::Adsb:     return QStringLiteral("ADS-B");
    case TranscriptCategory::Acars:    return QStringLiteral("ACARS");
    case TranscriptCategory::Pocsag:   return QStringLiteral("POCSAG");
    case TranscriptCategory::System:   return QStringLiteral("System");
    case TranscriptCategory::Other:    return QStringLiteral("Other");
    }
    return QStringLiteral("Other");
}

struct TranscriptSourceInfo {
    QString id;            // stable key: "p25", "am", "adsb", …
    QString displayName;   // UI label: "P25 Voice"
    TranscriptCategory category = TranscriptCategory::Other;
    bool enabled = true;   // false = reserved / not wired yet
};

struct TranscriptMessage {
    QDateTime timestamp;
    QString sourceId;
    QString sourceName;
    TranscriptCategory category = TranscriptCategory::Other;
    QString channel;       // talkgroup / frequency / address label
    QString text;
    QVariantMap meta;
};
