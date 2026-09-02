#!/usr/bin/env python3
"""Guard Phase 2 follow session identity ordering.

p25Phase2BeginNewPtt() derives p25CurrentCallSessionId from the receiver's
current talkgroup.  The metadata-follow path must therefore stamp the incoming
grant identity before every new-PTT call, or live speaker PCM can be attached to
TG 0 / epoch 0 and then starved or de-duped out of order.
"""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"


def main() -> int:
    text = MAIN.read_text(encoding="utf-8", errors="replace")
    marker = "static void p25CommitPhase2TrafficMetadataFollow"
    assert marker in text, "metadata follow helper missing"
    body = text.split(marker, 1)[1].split("static bool p25Phase2ShouldFreezeCqpskDiscrete", 1)[0]
    helper = "stampIncomingCallIdentityForNewPtt"
    assert helper in body, "incoming grant identity helper missing"
    assert "rx.p25VoiceTalkgroupId = followTg.talkgroupId;" in body, "TG identity is not stamped"
    assert "rx.p25VoiceTdmaSlot = followTg.tdmaSlot;" in body, "TDMA slot identity is not stamped"
    assert "rx.p25TrafficVoiceFreqHz = followTg.lastVoiceFreqHz;" in body, "voice frequency is not stamped"

    begin = "p25Phase2BeginNewPtt(rx, nowMs);"
    begin_positions = [idx for idx in range(len(body)) if body.startswith(begin, idx)]
    assert begin_positions, "no new-PTT calls found in metadata follow helper"
    for pos in begin_positions:
        preceding = body[max(0, pos - 700):pos]
        assert f"{helper}();" in preceding, (
            "p25Phase2BeginNewPtt must be preceded by incoming grant identity stamping"
        )

    print("verify_p25_phase2_follow_identity_before_ptt: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
