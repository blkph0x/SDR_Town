#include "P25TxSession.h"

#include <algorithm>

const char* p25TxStateLabel(P25TxState state) noexcept
{
    switch (state) {
        case P25TxState::Idle: return "Idle";
        case P25TxState::Armed: return "Armed";
        case P25TxState::Requesting: return "Requesting";
        case P25TxState::WaitGrant: return "WaitGrant";
        case P25TxState::TuningUplink: return "TuningUplink";
        case P25TxState::VoiceActive: return "VoiceActive";
        case P25TxState::Hang: return "Hang";
        case P25TxState::Error: return "Error";
        default: return "Unknown";
    }
}

const char* p25TxEventLabel(P25TxEvent event) noexcept
{
    switch (event) {
        case P25TxEvent::None: return "None";
        case P25TxEvent::Arm: return "Arm";
        case P25TxEvent::Disarm: return "Disarm";
        case P25TxEvent::PttPress: return "PttPress";
        case P25TxEvent::PttRelease: return "PttRelease";
        case P25TxEvent::GrantReceived: return "GrantReceived";
        case P25TxEvent::GrantTimeout: return "GrantTimeout";
        case P25TxEvent::GrantDenied: return "GrantDenied";
        case P25TxEvent::EncryptedGrant: return "EncryptedGrant";
        case P25TxEvent::TuneComplete: return "TuneComplete";
        case P25TxEvent::HangComplete: return "HangComplete";
        case P25TxEvent::Fault: return "Fault";
        case P25TxEvent::Reset: return "Reset";
        default: return "Unknown";
    }
}

namespace {

P25TxDecision stay(const P25TxSnapshot& snap, const char* reason = "")
{
    P25TxDecision d;
    d.nextState = snap.state;
    d.changed = false;
    d.reason = reason ? reason : "";
    d.statusLine = std::string(p25TxStateLabel(snap.state));
    if (!d.reason.empty()) {
        d.statusLine += " (";
        d.statusLine += d.reason;
        d.statusLine += ")";
    }
    return d;
}

P25TxDecision go(P25TxState next, const char* reason)
{
    P25TxDecision d;
    d.nextState = next;
    d.changed = true;
    d.reason = reason ? reason : "";
    d.statusLine = std::string(p25TxStateLabel(next));
    if (!d.reason.empty()) {
        d.statusLine += " — ";
        d.statusLine += d.reason;
    }
    return d;
}

bool maxPttExceeded(const P25TxSnapshot& snap) noexcept
{
    if (snap.pttPressedMs <= 0 || snap.nowMs <= 0) return false;
    const int maxSec = std::max(1, snap.config.maxPttSeconds);
    return (snap.nowMs - snap.pttPressedMs) > static_cast<int64_t>(maxSec) * 1000;
}

} // namespace

P25TxDecision evaluateP25Tx(const P25TxSnapshot& snap, P25TxEvent event) noexcept
{
    // Global reset / fault
    if (event == P25TxEvent::Reset) {
        auto d = go(P25TxState::Idle, "reset");
        d.stopVoiceTx = true;
        d.returnToControl = true;
        return d;
    }
    if (event == P25TxEvent::Fault) {
        auto d = go(P25TxState::Error, "fault");
        d.stopVoiceTx = true;
        d.returnToControl = true;
        return d;
    }
    if (event == P25TxEvent::Disarm) {
        auto d = go(P25TxState::Idle, "disarmed");
        d.stopVoiceTx = true;
        d.returnToControl = true;
        return d;
    }

    // Hard unkey on max PTT in any active TX path
    if (maxPttExceeded(snap) &&
        (snap.state == P25TxState::Requesting ||
         snap.state == P25TxState::WaitGrant ||
         snap.state == P25TxState::TuningUplink ||
         snap.state == P25TxState::VoiceActive)) {
        auto d = go(P25TxState::Hang, "max-ptt-timeout");
        d.stopVoiceTx = true;
        return d;
    }

    switch (snap.state) {
    case P25TxState::Idle:
        if (event == P25TxEvent::Arm) {
            if (!snap.config.canArm(snap.deviceCanTx)) {
                return go(P25TxState::Error, "arm-rejected-missing-identity-or-tx-device");
            }
            if (!snap.config.clearOnly) {
                return go(P25TxState::Error, "arm-rejected-clear-only-policy");
            }
            return go(P25TxState::Armed, "armed");
        }
        if (event == P25TxEvent::PttPress) {
            return stay(snap, "ptt-ignored-not-armed");
        }
        return stay(snap);

    case P25TxState::Armed:
        if (event == P25TxEvent::PttPress) {
            if (!snap.config.canArm(snap.deviceCanTx)) {
                return go(P25TxState::Error, "ptt-rejected-arm-invalid");
            }
            auto d = go(P25TxState::Requesting, "ptt-press");
            d.emitChannelRequest = true; // Sprint 6 will emit RF request
            return d;
        }
        return stay(snap);

    case P25TxState::Requesting:
        // Immediately enter wait-grant (Sprint 6 may stay here while building TSBK).
        if (event == P25TxEvent::None || event == P25TxEvent::PttPress) {
            return go(P25TxState::WaitGrant, "awaiting-grant");
        }
        if (event == P25TxEvent::PttRelease) {
            auto d = go(P25TxState::Hang, "ptt-release-before-grant");
            d.stopVoiceTx = true;
            return d;
        }
        return stay(snap);

    case P25TxState::WaitGrant: {
        if (event == P25TxEvent::PttRelease) {
            auto d = go(P25TxState::Hang, "ptt-release-wait-grant");
            return d;
        }
        if (event == P25TxEvent::EncryptedGrant ||
            (snap.grantValid && snap.grantEncrypted)) {
            auto d = go(P25TxState::Error, "encrypted-grant-refused");
            d.stopVoiceTx = true;
            d.returnToControl = true;
            return d;
        }
        if (event == P25TxEvent::GrantDenied) {
            return go(P25TxState::Armed, "grant-denied");
        }
        if (event == P25TxEvent::GrantTimeout) {
            return go(P25TxState::Armed, "grant-timeout");
        }
        // Timed wait without explicit timeout event
        if (snap.stateEnteredMs > 0 && snap.nowMs > 0 &&
            snap.nowMs - snap.stateEnteredMs >= snap.grantWaitTimeoutMs &&
            event != P25TxEvent::GrantReceived) {
            return go(P25TxState::Armed, "grant-timeout");
        }
        if (event == P25TxEvent::GrantReceived ||
            (snap.grantValid && !snap.grantEncrypted &&
             (snap.grantTalkgroupId == 0 ||
              snap.grantTalkgroupId == snap.config.talkgroupId))) {
            if (snap.config.clearOnly && snap.grantEncrypted) {
                return go(P25TxState::Error, "encrypted-grant-refused");
            }
            auto d = go(P25TxState::TuningUplink, "grant-accepted");
            d.tuneUplink = true;
            return d;
        }
        return stay(snap, "waiting-grant");
    }

    case P25TxState::TuningUplink:
        if (event == P25TxEvent::PttRelease) {
            auto d = go(P25TxState::Hang, "ptt-release-tuning");
            d.stopVoiceTx = true;
            d.returnToControl = true;
            return d;
        }
        if (event == P25TxEvent::TuneComplete || event == P25TxEvent::None) {
            // Sprint 0: treat tune as complete immediately when no RF layer.
            auto d = go(P25TxState::VoiceActive, "uplink-ready");
            d.startVoiceTx = true; // no-op until Sprint 4–5
            return d;
        }
        return stay(snap);

    case P25TxState::VoiceActive:
        if (event == P25TxEvent::PttRelease) {
            auto d = go(P25TxState::Hang, "ptt-release");
            d.stopVoiceTx = true;
            return d;
        }
        if (event == P25TxEvent::EncryptedGrant) {
            auto d = go(P25TxState::Error, "encrypted-mid-call");
            d.stopVoiceTx = true;
            return d;
        }
        return stay(snap, "tx-voice-stub");

    case P25TxState::Hang:
        if (event == P25TxEvent::HangComplete ||
            (snap.stateEnteredMs > 0 && snap.nowMs > 0 &&
             snap.nowMs - snap.stateEnteredMs >= snap.hangMs)) {
            auto d = go(P25TxState::Armed, "hang-complete");
            d.returnToControl = true;
            d.stopVoiceTx = true;
            return d;
        }
        if (event == P25TxEvent::PttPress) {
            // Re-key during hang
            auto d = go(P25TxState::Requesting, "rekey-during-hang");
            d.emitChannelRequest = true;
            return d;
        }
        return stay(snap, "hang");

    case P25TxState::Error:
        if (event == P25TxEvent::Arm) {
            if (snap.config.canArm(snap.deviceCanTx)) {
                return go(P25TxState::Armed, "re-armed-after-error");
            }
            return stay(snap, "still-cannot-arm");
        }
        if (event == P25TxEvent::PttPress) {
            return stay(snap, "ptt-ignored-error");
        }
        return stay(snap);

    default:
        return go(P25TxState::Idle, "unknown-state");
    }
}
