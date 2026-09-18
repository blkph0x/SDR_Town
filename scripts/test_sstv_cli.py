"""Actual CLI VIS diagnostics; optional independent upstream M1 recording gate."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np
import soundfile as sf


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=Path('build/bin/Release/SDR_Town.exe'))
    parser.add_argument('--reference', type=Path, help='Pinned colaclanth/sstv test/data/m1.ogg')
    args = parser.parse_args()

    def run(path):
        result = subprocess.run([str(args.exe.resolve()), '--cli', '--no-control-server',
                                 '--cmd', f'sstv inspect "{path}"'],
                                capture_output=True, text=True, encoding='utf-8', check=True, timeout=30)
        assert 'Probing SoapySDR' not in result.stdout, result.stdout
        return result.stdout

    def report(path):
        output = run(path)
        records = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
        assert len(records) == 1, output
        assert records[0]['decoder'] == 'sstv-vis' and not records[0]['imageDecoded'], records
        return records[0]

    with tempfile.TemporaryDirectory(prefix='sstv CLI ') as temporary:
        root = Path(temporary)
        silence = root / 'silence.wav'
        sf.write(silence, np.zeros(16000), 8000)
        assert report(silence)['headers'] == []
        unicode_file = root / 'recording-\u65e5\u672c.wav'
        sf.write(unicode_file, np.zeros(8000), 8000)
        assert report(unicode_file)['headers'] == []
        stereo = root / 'stereo.wav'
        sf.write(stereo, np.zeros((8000, 2)), 8000)
        assert 'must be mono' in run(stereo)
        low = root / 'bad rate.wav'
        sf.write(low, np.zeros(4000), 4000)
        assert '8..96 kHz' in run(low)
        long_file = root / 'long.wav'
        sf.write(long_file, np.zeros(8000*121), 8000)
        assert 'exceeds 120 seconds' in run(long_file)
        oversized = root / 'oversized.wav'
        with oversized.open('wb') as stream:
            stream.truncate(64*1024*1024+1)
        assert 'exceeds 64 MiB' in run(oversized)
        broken = root / 'broken.wav'
        broken.write_bytes(b'not audio')
        assert 'Cannot open' in run(broken)
        assert 'Cannot open' in run(root / 'missing.wav')
        if args.reference:
            # Local-only derivative for tests; do not redistribute upstream audio.
            source = args.reference.resolve()
            sha = hashlib.sha256(source.read_bytes()).hexdigest()
            assert sha == '051bdb63f6f84abd3abdf90281361bf79bd1c95ad0c4e2255fb2657b1bd37c16', 'Reference fixture hash mismatch'
            with sf.SoundFile(source) as audio:
                rate = audio.samplerate
                samples = audio.read(rate*3, dtype='float32', always_2d=True)
            mono = samples.mean(axis=1)
            recording = root / 'independent M1.wav'
            sf.write(recording, mono, rate, subtype='FLOAT')
            decoded = report(recording)
            assert len(decoded['headers']) == 1, decoded
            assert decoded['headers'][0]['vis'] == 44, decoded
            assert decoded['headers'][0]['mode'] == 'Martin M1', decoded
            print('PASS independent M1:', json.dumps(decoded))
            print('reference SHA256:', sha)
            start = decoded['headers'][0]['startSample']
            truncated = root / 'truncated header.wav'
            sf.write(truncated, mono[:start+int(rate*0.88)], rate, subtype='FLOAT')
            assert report(truncated)['headers'] == []
        print('PASS SSTV CLI: silence, channels, rate, duration, malformed/missing inputs')
        if not args.reference:
            print('NOT RUN: independent recording gate (supply --reference)')


if __name__ == '__main__':
    main()
