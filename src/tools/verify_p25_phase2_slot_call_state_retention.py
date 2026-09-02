#!/usr/bin/env python3
"""Guard Phase-2 per-slot MAC_ACTIVE/traffic-security retention."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="replace")
CPP = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")


checks = {
    "global active retention field": "bool m_phase2SessionActiveSeen = false;" in HEADER,
    "global traffic security retention field": "bool m_phase2SessionTrafficSecurityKnown = false;" in HEADER,
    "slot active retention field": "std::array<bool, 2> m_phase2SlotActiveSeen{};" in HEADER,
    "slot traffic security retention field": "std::array<bool, 2> m_phase2SlotTrafficSecurityKnown{};" in HEADER,
    "retention scoring helper": "int phase2RetainedSessionScore(const Phase2SessionState& session)" in CPP,
    "slot retained-state array": "std::array<bool, 2> slotHasRetainedState{};" in CPP,
    "detect any retained slot state": "const bool anySlotHasRetainedState =" in CPP,
    "legacy global fallback only before slot ownership": "const bool useGlobalFallback = !useSlotState && !anySlotHasRetainedState;" in CPP,
    "seed slot active state without cross-slot smear": "session.activeSeen = useSlotState ? m_phase2SlotActiveSeen[ts] : (useGlobalFallback ? m_phase2SessionActiveSeen : false);" in CPP,
    "seed slot traffic security without cross-slot smear": "session.trafficSecurityKnown = useSlotState ? m_phase2SlotTrafficSecurityKnown[ts] : (useGlobalFallback ? m_phase2SessionTrafficSecurityKnown : false);" in CPP,
    "persist slot active state": "m_phase2SlotActiveSeen[ts] = slotSessions[ts].activeSeen;" in CPP,
    "persist slot traffic security state": "m_phase2SlotTrafficSecurityKnown[ts] = slotSessions[ts].trafficSecurityKnown;" in CPP,
    "persist global active state": "m_phase2SessionActiveSeen = retainedSession->activeSeen;" in CPP,
    "persist global traffic security state": "m_phase2SessionTrafficSecurityKnown = retainedSession->trafficSecurityKnown;" in CPP,
    "slot state detects active calls": "m_phase2SlotActiveSeen[ts] ||" in CPP,
    "slot state detects traffic security": "m_phase2SlotTrafficSecurityKnown[ts] ||" in CPP,
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit("Phase-2 slot call-state retention guard failed: " + ", ".join(missing))

if CPP.count("const bool useGlobalFallback = !useSlotState && !anySlotHasRetainedState;") < 2:
    raise SystemExit("Phase-2 slot call-state retention guard failed: both Phase-2 decode paths must isolate empty slots")

print("Phase-2 slot call-state retention guard passed")
