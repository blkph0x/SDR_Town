#pragma once

#include "P25TxConfig.h"

#include <cstdint>
#include <string>

// Sprint 0 stub state machine for Phase 2 trunk clear TX.
// Transitions are real; RF/encode/modulate actions are no-ops until later sprints.

enum class P25TxState : int {
    Idle = 0,
    Armed,
    Requesting,
    WaitGrant,
    TuningUplink,
    VoiceActive,
    Hang,
    Error,
};

enum class P25TxEvent : int {
    None = 0,
    Arm,
    Disarm,
    PttPress,
    PttRelease,
    GrantReceived,
    GrantTimeout,
    GrantDenied,
    EncryptedGrant,
    TuneComplete,
    HangComplete,
    Fault,
    Reset,
};

struct P25TxSnapshot {
    P25TxConfig config;
    P25TxState state = P25TxState::Idle;
    int64_t nowMs = 0;
    int64_t stateEnteredMs = 0;
    int64_t pttPressedMs = 0;

    bool deviceCanTx = false;
    bool pttHeld = false;

    // Latest clear grant observation from RX control path (Sprint 6 will wire).
    bool grantValid = false;
    bool grantEncrypted = false;
    uint32_t grantTalkgroupId = 0;
    double grantUplinkHz = 0.0;
    double grantDownlinkHz = 0.0;
    bool grantSlotKnown = false;
    uint8_t grantSlot = 0;

    int grantWaitTimeoutMs = 8000;
    int hangMs = 500;
};

struct P25TxDecision {
    P25TxState nextState = P25TxState::Idle;
    bool changed = false;
    bool emitChannelRequest = false;   // Sprint 6: build TSBK/MAC request
    bool startVoiceTx = false;         // Sprint 4–5: framer+H-CPM+writeStream
    bool stopVoiceTx = false;
    bool tuneUplink = false;
    bool returnToControl = false;
    std::string reason;
    std::string statusLine;
};

const char* p25TxStateLabel(P25TxState state) noexcept;
const char* p25TxEventLabel(P25TxEvent event) noexcept;

// Pure transition function (unit-testable, no RF).
P25TxDecision evaluateP25Tx(const P25TxSnapshot& snap, P25TxEvent event) noexcept;
