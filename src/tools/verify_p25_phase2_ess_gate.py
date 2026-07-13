#!/usr/bin/env python3
"""
Regression checks for the Phase 2 ESS mapping/gating patch.
This is deliberately dependency-free: it verifies the fragile index ordering and
policy rules that caused 12/12 superframe+mask windows to poison clear audio as
'encrypted' without MAC/ESS trust.
"""


def map_ess_symbols(essa, essb_seen):
    """Return the RS symbol indexes that should be populated for ESS.

    essa: list of 28 six-bit symbols in over-the-air ESS-A order.
    essb_seen: dict {0..3: list of four six-bit ESS-B symbols in B1..B4 OTA order}
    """
    symbols = [0] * 63
    erasures = []
    for i in range(28):
        symbols[i] = essa[27 - i] & 0x3F

    def place(burst, base):
        if burst in essb_seen:
            b = essb_seen[burst]
            for i in range(4):
                symbols[base + i] = b[3 - i] & 0x3F
        else:
            erasures.extend(range(base, base + 4))

    place(3, 28)  # B4
    place(2, 32)  # B3
    place(1, 36)  # B2
    place(0, 40)  # B1
    return symbols, erasures


def accept_voice_ess(mac_crc_seen, trusted, tentative, candidate):
    """Mirror the C++ trust policy, reduced to core fields."""
    if not candidate["known"] or not candidate["fec"]:
        return trusted, tentative, False
    if mac_crc_seen or not candidate["encrypted"]:
        return candidate, None, True
    if tentative and tentative["core"] == candidate["core"]:
        repeats = tentative["repeats"] + 1
    else:
        repeats = 1
    tentative = {"core": candidate["core"], "repeats": repeats}
    if repeats >= 2:
        return candidate, tentative, True
    return trusted, tentative, False


def main():
    essa = list(range(28))
    essb = {
        0: [10, 11, 12, 13],
        1: [20, 21, 22, 23],
        2: [30, 31, 32, 33],
        3: [40, 41, 42, 43],
    }
    symbols, erasures = map_ess_symbols(essa, essb)
    assert symbols[0:4] == [27, 26, 25, 24]
    assert symbols[24:28] == [3, 2, 1, 0]
    assert symbols[28:32] == [43, 42, 41, 40]  # B4 reversed
    assert symbols[32:36] == [33, 32, 31, 30]  # B3 reversed
    assert symbols[36:40] == [23, 22, 21, 20]  # B2 reversed
    assert symbols[40:44] == [13, 12, 11, 10]  # B1 reversed
    assert erasures == []

    symbols, erasures = map_ess_symbols(essa, {0: essb[0]})
    assert erasures == list(range(28, 40))
    assert symbols[40:44] == [13, 12, 11, 10]

    clear = {"known": True, "fec": True, "encrypted": False, "core": (0x80, 0, bytes(9))}
    enc = {"known": True, "fec": True, "encrypted": True, "core": (0x84, 7, bytes(range(9)))}
    trusted, tentative, accepted = accept_voice_ess(False, None, None, clear)
    assert accepted and trusted is clear
    trusted, tentative, accepted = accept_voice_ess(False, None, None, enc)
    assert not accepted and trusted is None and tentative["repeats"] == 1
    trusted, tentative, accepted = accept_voice_ess(False, trusted, tentative, enc)
    assert accepted and trusted is enc
    trusted, tentative, accepted = accept_voice_ess(True, None, None, enc)
    assert accepted and trusted is enc

    print("P25 Phase 2 ESS mapping/gating regression: PASS")


if __name__ == "__main__":
    main()
