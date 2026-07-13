#!/usr/bin/env python3
"""
Regression guard for the Phase 2 12-burst superframe slot mapping.

sdrtrunk's SuperFrameFragment maps each 4-timeslot fragment as A,B,C,D,
but for the final fragment it presents D before C for logical channel order.
Therefore the last two physical bursts are not simple even/odd slot parity:
index 10 is traffic slot 1 and index 11 is traffic slot 0.
"""

EXPECTED_GRANT_SLOT = [
    0, 1, 0, 1,
    0, 1, 0, 1,
    0, 1, 1, 0,
]
EXPECTED_VOICE_SEQUENCE_INDEX = [
    0, 0, 1, 1,
    2, 2, 3, 3,
    4, 4, 5, 5,
]


def traffic_slot(index: int) -> int:
    return 0 if index % 12 in (0, 2, 4, 6, 8, 11) else 1


def voice_sequence_index(index: int) -> int:
    return [0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5][index % 12]


def main() -> int:
    got_slots = [traffic_slot(i) for i in range(12)]
    got_seq = [voice_sequence_index(i) for i in range(12)]
    if got_slots != EXPECTED_GRANT_SLOT:
        raise SystemExit(f"grant slot mapping mismatch: {got_slots} != {EXPECTED_GRANT_SLOT}")
    if got_seq != EXPECTED_VOICE_SEQUENCE_INDEX:
        raise SystemExit(f"voice sequence mapping mismatch: {got_seq} != {EXPECTED_VOICE_SEQUENCE_INDEX}")
    if [i & 1 for i in range(12)] == EXPECTED_GRANT_SLOT:
        raise SystemExit("test invalid: simple parity should not match sdrtrunk final-fragment mapping")
    print("P25 Phase 2 sdrtrunk-compatible slot mapping regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
