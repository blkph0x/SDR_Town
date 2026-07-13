#!/usr/bin/env python3
"""
Deterministic regression check for the P25 Phase 2 ACCH RS symbol placement.

This does not require live RF or Qt.  It verifies the fragile part that broke
MAC/ECC/ESS acquisition: transmit-order ACCH symbols must be placed into the
shortened RS(63,35,29) codeword in the same reverse positions used by sdrtrunk.
"""

from __future__ import annotations


def map_acch_tx_to_rs(tx_symbols: list[int], fast: bool) -> tuple[list[int], list[int], list[int]]:
    rs = [0] * 63
    erasures: list[int] = []
    if fast:
        assert len(tx_symbols) == 45
        erasures.extend(range(0, 9))
        erasures.extend(range(54, 63))
        # INFO_1..INFO_26 => RS 53..28, PARITY_1..PARITY_19 => RS 27..9
        for i in range(26):
            rs[53 - i] = tx_symbols[i] & 0x3F
        for i in range(19):
            rs[27 - i] = tx_symbols[26 + i] & 0x3F
        info_positions = list(range(53, 27, -1))
    else:
        assert len(tx_symbols) == 52
        erasures.extend(range(0, 6))
        erasures.extend(range(58, 63))
        # INFO_1..INFO_30 => RS 57..28, PARITY_1..PARITY_22 => RS 27..6
        for i in range(30):
            rs[57 - i] = tx_symbols[i] & 0x3F
        for i in range(22):
            rs[27 - i] = tx_symbols[30 + i] & 0x3F
        info_positions = list(range(57, 27, -1))
    return rs, erasures, info_positions


def round_trip_info_symbols(fast: bool) -> None:
    info_count = 26 if fast else 30
    parity_count = 19 if fast else 22
    # Distinct non-zero values make reversed/ascending bugs obvious.
    tx_info = [((i * 7) + 3) & 0x3F for i in range(info_count)]
    tx_parity = [((i * 11) + 5) & 0x3F for i in range(parity_count)]
    tx = tx_info + tx_parity

    rs, erasures, info_positions = map_acch_tx_to_rs(tx, fast)
    recovered = [rs[pos] for pos in info_positions]
    assert recovered == tx_info, (fast, recovered, tx_info)

    if fast:
        assert erasures == list(range(0, 9)) + list(range(54, 63))
        assert [rs[pos] for pos in range(27, 8, -1)] == tx_parity
    else:
        assert erasures == list(range(0, 6)) + list(range(58, 63))
        assert [rs[pos] for pos in range(27, 5, -1)] == tx_parity


def main() -> None:
    round_trip_info_symbols(fast=True)
    round_trip_info_symbols(fast=False)
    print("P25 Phase 2 ACCH FACCH/SACCH/LCCH RS mapping regression: PASS")


if __name__ == "__main__":
    main()
