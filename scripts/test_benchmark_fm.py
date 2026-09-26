import json
import unittest
from benchmark_fm import parse_rows


class ReportValidation(unittest.TestCase):
    def rows(self):
        return [{"mode": mode, "sampleRate": rate, "placement": placement,
                 "blockerExcessDb": power, "wantedGainDb": 0,
                 "blockerAudioRelativeDb": -60, "differenceRelativeDb": -50,
                 "processingUs": 1000, "realtimeRatio": .005,
                 "inputSamples": 400000, "audioSamples": 9600,
                 "channelizerUs": 900, "discriminatorUs": 50,
                 "resamplerUs": 25, "postAudioUs": 25}
                for mode in ("NFM", "WFM") for rate in (2048000, 2400000, 10000000)
                for placement in ("adjacent", "first_image") for power in (0, 20, 40)]

    def text(self, rows):
        return "Catch output ignored\n" + "\n".join("FM_BENCH " + json.dumps(row) for row in rows)

    def test_complete(self):
        self.assertEqual(len(parse_rows(self.text(self.rows()))), 36)

    def test_incomplete(self):
        with self.assertRaises(ValueError):
            parse_rows(self.text(self.rows()[:-1]))

    def test_duplicate(self):
        rows = self.rows()
        rows[-1] = rows[0]
        with self.assertRaises(ValueError):
            parse_rows(self.text(rows))

    def test_bad_numbers(self):
        for field, value in (("wantedGainDb", float("nan")), ("processingUs", 0),
                             ("realtimeRatio", float("inf")), ("audioSamples", 0)):
            rows = self.rows()
            rows[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                parse_rows(self.text(rows))


if __name__ == "__main__":
    unittest.main()
