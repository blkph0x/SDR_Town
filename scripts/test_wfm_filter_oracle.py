import unittest
import numpy as np
from analyze_wfm_filter import causal_filter, taps, discriminator


class OracleTests(unittest.TestCase):
    def test_convolution(self):
        rng = np.random.default_rng(151)
        for n in (1, 2, 31, 1024):
            x = rng.normal(size=n) + 1j*rng.normal(size=n)
            h = rng.normal(size=65)
            np.testing.assert_allclose(causal_filter(x, h), np.convolve(x, h)[:n], atol=1e-12)

    def test_design(self):
        h = taps(10000000, 180000)
        self.assertEqual(len(h), 321)
        self.assertEqual(h[0], 0)
        np.testing.assert_array_equal(h, h[::-1])
        self.assertAlmostEqual(float(h.sum(dtype=np.float64)), 1, places=6)
        with self.assertRaises(ValueError):
            taps(10000000, 180000, 100)

    def test_discriminator(self):
        x = np.exp(2j*np.pi*1700*np.arange(4096)/200000)
        np.testing.assert_allclose(discriminator(x, 200000), 1700/75000, atol=1e-12)


if __name__ == '__main__':
    unittest.main()
