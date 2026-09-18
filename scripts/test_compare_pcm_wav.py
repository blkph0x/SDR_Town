import math
import struct
import tempfile
import unittest
from pathlib import Path

from compare_pcm_wav import read_wav


class WavReaderTests(unittest.TestCase):
    def read(self, codec, bits, data, truncate=False):
        fmt = struct.pack("<HHIIHH", codec, 1, 48000, 48000 * bits // 8, bits // 8, bits)
        body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt
        body += b"JUNK" + struct.pack("<I", 1) + b"x\0"
        body += b"data" + struct.pack("<I", len(data)) + data
        if len(data) & 1:
            body += b"\0"
        file = b"RIFF" + struct.pack("<I", len(body)) + body
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "test.wav"
            path.write_bytes(file[:-1] if truncate else file)
            return read_wav(path)

    def test_pcm16_and_float_equivalent(self):
        integer = self.read(1, 16, struct.pack("<hhh", -32768, 0, 16384))
        floating = self.read(3, 32, struct.pack("<fff", -1, 0, .5))
        self.assertEqual(integer, floating)

    def test_truncated(self):
        with self.assertRaises(ValueError):
            self.read(3, 32, struct.pack("<f", .5), truncate=True)

    def test_partial_frame(self):
        with self.assertRaises(ValueError):
            self.read(1, 16, b"x")

    def test_nonfinite(self):
        for value in (math.nan, math.inf, -math.inf):
            with self.assertRaises(ValueError):
                self.read(3, 32, struct.pack("<f", value))

    def test_unsupported_codec(self):
        with self.assertRaises(ValueError):
            self.read(6, 8, b"xx")


if __name__ == "__main__":
    unittest.main()
