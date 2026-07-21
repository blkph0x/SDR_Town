#pragma once

#include "TranscriptTypes.h"

#include <QObject>
#include <QString>

// Abstract feed producers implement this (or simply call TranscriptHub::append).
// The Decode Log window never depends on concrete decoder types.
class ITranscriptSource
{
public:
    virtual ~ITranscriptSource() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual TranscriptCategory category() const = 0;
    virtual bool isEnabled() const { return true; }

    TranscriptSourceInfo info() const
    {
        return TranscriptSourceInfo{id(), displayName(), category(), isEnabled()};
    }
};

// QObject base for sources that connect signals into the hub.
class TranscriptSourceBase : public QObject, public ITranscriptSource
{
public:
    explicit TranscriptSourceBase(QObject* parent = nullptr) : QObject(parent) {}
};
