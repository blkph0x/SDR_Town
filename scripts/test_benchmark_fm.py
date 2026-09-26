import json
import unittest
from benchmark_fm import parse_rows, parse_sweep
import itertools


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


class SweepValidation(unittest.TestCase):
    def rows(self):
        return [dict(zip(("sampleRate", "bandwidthHz", "deviationHz", "side", "foldHz"), key),
                     actualIqRate=200000, offsetHz=230000, blockerExcessDb=40,
                     wantedGainDb=0, differenceRelativeDb=-50, realtimeRatio=.2, audioSamples=9600)
                for key in itertools.product((2048000, 2400000, 10000000),
                    (150000, 180000, 220000), (50000, 75000), (-1, 1), (-30000, 30000))]

    def text(self, rows):
        return '\n'.join('WFM_SWEEP ' + json.dumps(row) for row in rows)

    def test_complete(self):
        self.assertEqual(len(parse_sweep(self.text(self.rows()))), 72)

    def test_bad_matrix(self):
        rows=self.rows()
        for bad in (rows[:-1], rows[:-1]+[rows[0]]):
            with self.assertRaises(ValueError):
                parse_sweep(self.text(bad))

    def test_bad_values(self):
        for field,value in (("wantedGainDb", float('nan')), ("realtimeRatio", 0),
                            ("audioSamples", 0), ("blockerExcessDb", 20)):
            rows=self.rows();rows[0][field]=value
            with self.subTest(field=field), self.assertRaises(ValueError):
                parse_sweep(self.text(rows))


if __name__ == "__main__":
    unittest.main()
