"""DEC-0117: independent recording -> synthetic RF -> production auto route -> image.

RF modulation is synthetic, not an off-air hardware qualification. Reference
pixels are compared after demodulation; small filter-induced shifts are expected.
"""
import gzip
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import hilbert, resample_poly


def main():
    root = Path(__file__).resolve().parents[1]
    binary = root / 'build/bin/Release'
    fixture = root / 'build/reference-sstv-rust/tests/assets/real_recording.wav.gz'
    samples, rate = sf.read(io.BytesIO(gzip.decompress(fixture.read_bytes())), dtype='float64')
    if samples.ndim == 2:
        samples = samples[:, 0]
    common = np.gcd(rate, 96000)
    audio = resample_poly(samples, 96000 // common, rate // common)
    audio *= 0.7 / max(np.max(np.abs(audio)), 1e-9)
    with tempfile.TemporaryDirectory(prefix='sstv-rf-') as directory:
        tmp = Path(directory)
        reference = tmp / 'reference.pcm'
        np.rint(np.clip(audio, -1, 32767/32768) * 32768).astype('<i2').tofile(reference)
        run = subprocess.run([str(binary / 'sdrtown_sstv.exe'), str(reference), '96000', str(tmp / 'ref'), 'auto'],
                             capture_output=True, text=True, check=True, timeout=60)
        expected = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
        assert expected, run.stdout
        analytic = hilbert(audio)
        for mode in ('USB', 'LSB', 'NFM', 'AM'):
            if mode == 'NFM':
                iq = 0.5 * np.exp(1j * np.cumsum(2 * np.pi * 2500 * audio / 96000))
            elif mode == 'AM':
                iq = (0.5 + 0.4 * audio).astype(np.complex128)
            else:
                iq = 0.5 * (analytic if mode == 'USB' else np.conj(analytic))
            # Independent fixed noise prevents a mathematically perfect opposite
            # sideband's finite stopband leakage being mistaken for a second signal.
            rng = np.random.default_rng(117)
            iq += 0.0001 * (rng.normal(size=iq.size) + 1j * rng.normal(size=iq.size))
            iq.astype('<c8').tofile(tmp / 'rf.cf32')
            env = dict(os.environ, SDR_TOWN_SSTV_RF_IQ=str(tmp / 'rf.cf32'),
                       SDR_TOWN_SSTV_RF_PCM=str(tmp / 'audio.pcm'), SDR_TOWN_SSTV_RF_EXPECT=mode,
                       SDR_TOWN_SSTV_RF_REQUEST='AM' if mode == 'AM' else 'auto')
            check = subprocess.run([str(binary / 'sdr_town_tests.exe'), '[sstv-rf-recording]'],
                                   env=env, capture_output=True, text=True, timeout=120)
            assert check.returncode == 0, check.stdout + check.stderr
            decoded = subprocess.run([str(binary / 'sdrtown_sstv.exe'), str(tmp / 'audio.pcm'), '48000', str(tmp / mode), 'auto'],
                                     capture_output=True, text=True, check=True, timeout=60)
            actual = [json.loads(line) for line in decoded.stdout.splitlines() if line.startswith('{')]
            # This recording continues with noise after the complete Robot36.
            # Existing line-sync Auto can report provisional partial images there;
            # compare complete pictures, and report (do not hide) that tail.
            print(mode, 'provisional tail:', [(a['mode'], a['rows']) for a in actual if not a['complete']])
            completed = [a for a in actual if a['complete']]
            reference_images = [e for e in expected if e['complete']]
            assert len(completed) == len(reference_images) and completed, (mode, actual, expected)
            for a, e in zip(completed, reference_images):
                assert (a['mode'], a['rows'], a['complete']) == (e['mode'], e['rows'], e['complete']), (mode, a, e)
                ref = np.fromfile(tmp / 'ref' / e['file'], dtype=np.uint8).astype(float)
                got = np.fromfile(tmp / mode / a['file'], dtype=np.uint8).astype(float)
                error = np.mean(np.abs(ref - got))
                print(f'{mode}: {a["mode"]}, {a["rows"]} rows, mean RGB error={error:.3f}/255')
                # DEC-0117 test budget: <5% full-scale average pixel error. This
                # measures end-to-end image fidelity, not RF classifier confidence.
                assert error < 255 * 0.05, (mode, error)


if __name__ == '__main__':
    main()
