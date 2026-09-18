"""Independent DCS word/bit-order oracle, not a live DCS decoder.

ETSI TS 103 236 V1.1.1 sections 4.2.1-4.2.3, table 2. Golay polynomial
x^11+x^10+x^6+x^5+x^4+x^2+1 (0xC75); see OP25 op25_golay.h syndrome.
No upstream implementation code is copied. Optional ideal discriminator WAVs
retain DC and are not RF captures or a model of a radio's transmit shaping.
"""
import argparse
import json
from pathlib import Path
import struct
import wave


MASK = (1 << 23) - 1
# Literal independent table-2 vectors, represented bit23..bit1.
VECTORS = {
    0o023: 0b11101100011100000010011,
    0o025: 0b11010110111100000010101,
    0o026: 0b11001011101100000010110,
    0o754: 0b01000001111100111101100,
}


def syndrome(word):
    for bit in range(22, 10, -1):
        if word & (1 << bit):
            word ^= 0xC75 << (bit - 11)
    return word


# Solve the parity equations, rather than assume the P25 systematic bit layout.
PARITY = {syndrome(p << 12): p for p in range(1 << 11)}
assert len(PARITY) == 2048


def encode(code):
    if not 0 <= code <= 0o777:
        raise ValueError("DCS payload is nine bits")
    data = 0x800 | code
    return data | (PARITY[syndrome(data)] << 12)


def air_bits(word, inverted=False):
    return [((word >> bit) & 1) ^ inverted for bit in range(23)]


def verify():
    for code, expected in VECTORS.items():
        assert encode(code) == expected, oct(code)
        assert syndrome(expected) == 0
        bits = air_bits(expected)
        assert sum(bit << i for i, bit in enumerate(bits)) == expected
        assert all(a != b for a, b in zip(bits, air_bits(expected, True)))
        # Reversing time order is demonstrably not inverting deviation polarity.
        assert list(reversed(bits)) != air_bits(expected, True)
        for bit in range(23):
            assert syndrome(expected ^ (1 << bit)) != 0
    for code in range(512):
        word = encode(code)
        assert word & 4095 == (0x800 | code)
        assert syndrome(word) == 0
    # SDRTrunk N023 decimal table entry equals the received shift-left register
    # after 23 LSB-first symbols. Its I023 entry is instead the unreversed word.
    shifted = 0
    for bit in air_bits(VECTORS[0o023]):
        shifted = (shifted << 1) | bit
    assert shifted == 6557239
    assert VECTORS[0o023] == 7747603
    assert (shifted ^ MASK) != 7747603
    print("PASS: DCS table vectors, 512 payload syndromes, bit order, polarity and single-bit detection")


def write_fixtures(directory):
    directory.mkdir(parents=True, exist_ok=True)
    rate, seconds = 8000, 5
    for code in VECTORS:
        for inverted in (False, True):
            bits = air_bits(encode(code), inverted)
            path = directory / f"dcs_{code:03o}_{'inverted' if inverted else 'normal'}.wav"
            # 134.4 baud = 672/5. Integer arithmetic avoids symbol-clock drift.
            pcm = [6000 if bits[(n * 672 // (5 * rate)) % 23] else -6000
                   for n in range(rate * seconds)]
            with wave.open(str(path), "wb") as output:
                output.setnchannels(1)
                output.setsampwidth(2)
                output.setframerate(rate)
                output.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
    (directory / "manifest.json").write_text(json.dumps({
        "source": "ideal synthetic discriminator; NOT received RF",
        "standard": "ETSI TS 103 236 V1.1.1, 4.2",
        "sampleRate": rate, "seconds": seconds, "baud": 134.4,
        "polarity": "normal: positive for 1, negative for 0; inverted: negated",
        "codesOctal": [f"{code:03o}" for code in VECTORS],
        "transmitShaping": False,
    }, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Write bounded synthetic discriminator fixtures")
    args = parser.parse_args()
    verify()
    if args.output:
        write_fixtures(args.output)
