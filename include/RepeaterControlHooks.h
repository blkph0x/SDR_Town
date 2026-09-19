#pragma once
#include "ControlEventLog.h"
#include "CtcssDecoder.h"
#include "DcsDecoder.h"
#include "DtmfDecoder.h"
#include <cmath>
#include <string>

// Shared helpers for opt-in repeater control monitoring (receive-only).

inline std::string dcsKeyFromSnapshot(const DcsSnapshot& dcs)
{
    if (dcs.identities.empty()) return {};
    return dcsLabel(preferredDcsIdentity(dcs.identities));
}

inline void drainDtmfEvents(DtmfDecoder& decoder, ControlEventLog& log,
                            ControlEvent::Channel channel, double freqHz)
{
    for (const auto& ev : decoder.takePendingEvents()) {
        // List view shows completed sequences only — per-digit spam is not useful.
        if (ev.kind != DtmfDecoder::PendingEvent::Kind::Sequence) continue;
        if (ev.sequence.empty()) continue;
        ControlEvent out;
        out.ms = ev.ms;
        out.channel = channel;
        out.freqHz = freqHz;
        out.kind = ControlEvent::Kind::DtmfSequence;
        out.detail = ev.sequence;
        log.push(std::move(out));
    }
}

inline void noteToneChanges(ControlEventLog& log, ControlEvent::Channel channel, double freqHz,
                            const CtcssSnapshot& ctcss, double& lastCtcssHz,
                            const DcsSnapshot& dcs, std::string& lastDcsKey,
                            bool logTones)
{
    if (!logTones) return;
    const double ctcssHz = ctcss.frequencyHz > 0.0 ? ctcss.frequencyHz : 0.0;
    if (std::abs(ctcssHz - lastCtcssHz) > 0.05) {
        if (lastCtcssHz >= 0.0 || ctcssHz > 0.0) {
            ControlEvent e;
            e.ms = ctcss.updatedMs != 0 ? ctcss.updatedMs : dcs.updatedMs;
            e.kind = ControlEvent::Kind::CtcssChange;
            e.channel = channel;
            e.freqHz = freqHz;
            e.detail = ctcssHz > 0.0 ? (std::to_string(ctcssHz) + " Hz") : "clear";
            log.push(std::move(e));
        }
        lastCtcssHz = ctcssHz;
    }
    const std::string dcsKey = dcsKeyFromSnapshot(dcs);
    if (dcsKey != lastDcsKey) {
        if (!lastDcsKey.empty() || !dcsKey.empty()) {
            ControlEvent e;
            e.ms = dcs.updatedMs != 0 ? dcs.updatedMs : ctcss.updatedMs;
            e.kind = ControlEvent::Kind::DcsChange;
            e.channel = channel;
            e.freqHz = freqHz;
            e.detail = dcsKey.empty() ? "clear" : dcsKey;
            log.push(std::move(e));
        }
        lastDcsKey = dcsKey;
    }
}

inline void noteCarrier(ControlEventLog& log, ControlEvent::Channel channel, double freqHz,
                        bool open, bool& lastOpen, bool logCarrier, int64_t ms)
{
    if (!logCarrier || open == lastOpen) return;
    ControlEvent e;
    e.ms = ms;
    e.kind = open ? ControlEvent::Kind::CarrierOpen : ControlEvent::Kind::CarrierClose;
    e.channel = channel;
    e.freqHz = freqHz;
    e.detail = open ? "open" : "closed";
    log.push(std::move(e));
    lastOpen = open;
}
