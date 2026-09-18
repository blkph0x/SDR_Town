"""Actual application image decode, pixel parity, and bounded failure cases."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from PIL import Image
import soundfile as sf


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=Path('build/bin/Release/SDR_Town.exe'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='sstv images ') as directory:
        temp = Path(directory)

        def run(source, output, mode='auto'):
            result = subprocess.run([str(args.exe.resolve()), '--cli', '--no-control-server', '--cmd',
                                     f'sstv decode "{source}" "{output}" {mode}'],
                                    capture_output=True, text=True, encoding='utf-8', timeout=140, check=True)
            assert 'Probing SoapySDR' not in result.stdout, result.stdout
            return result.stdout

        for mode, recording in [
            ('robot36', root / 'build/reference-sstv-rust/tests/assets/real_recording.wav.gz'),
            ('martin1', root / 'build/reference-sstv/test/data/m1.ogg'),
        ]:
            source = io.BytesIO(gzip.decompress(recording.read_bytes())) if recording.suffix == '.gz' else recording
            samples, rate = sf.read(source, dtype='int16')
            if samples.ndim == 2:
                samples = samples[:, 0]
            audio = temp / f'{mode}-\u65e5\u672c.wav'
            sf.write(audio, samples, rate, subtype='PCM_16')
            for selection in (mode, 'auto'):
                output = temp / f'{mode}-{selection}'
                stdout = run(audio, output, selection)
                assert 'SSTV error:' not in stdout, stdout
                report = json.loads((output / 'sstv-report.json').read_text())
                assert len(report['images']) == 1, report
                image = report['images'][0]
                assert image['complete'] and image['mode'] == mode, report
                rgb = np.asarray(Image.open(output / image['file']).convert('RGB'))
                pcm = temp / f'{mode}-{selection}.pcm'
                pcm.write_bytes(samples.astype('<i2').tobytes())
                raw_output = temp / f'{mode}-{selection}-raw'
                helper = args.exe.resolve().parent / 'sdrtown_sstv.exe'
                direct = subprocess.run([str(helper), str(pcm), str(rate), str(raw_output), selection],
                                        capture_output=True, text=True, check=True, timeout=120)
                direct_image = json.loads(direct.stdout.splitlines()[0])
                assert rgb.tobytes() == (raw_output / direct_image['file']).read_bytes(), (mode, selection)
                if mode == 'robot36':
                    expected = np.asarray(Image.open(root / 'build/reference-sstv-rust/examples/patch.png').convert('RGB'))
                    error = float(np.abs(rgb.astype(float) - expected.astype(float)).mean())
                    assert error < 15, (selection, error)
                    print('Robot36 reference MAE', selection, error)
                assert hashlib.sha256(rgb.tobytes()).hexdigest() == image['rgbSha256']
                assert 'already exists' in run(audio, output, selection)
            short = temp / 'partial.wav'
            sf.write(short, samples[:rate*15], rate, subtype='PCM_16')
            output = temp / f'{mode}-partial'
            assert 'SSTV error:' not in run(short, output, mode)
            report = json.loads((output / 'sstv-report.json').read_text())
            assert report['images'] and not report['images'][0]['complete'], report
            assert report['images'][0]['file'].endswith('.partial.png'), report
            print('PASS', mode, 'forced/auto/Unicode/pixel parity/no overwrite/partial')

        audio = temp / 'negative.wav'
        sf.write(audio, np.zeros(8000), 8000)
        output = temp / 'silent'
        assert 'SSTV error:' not in run(audio, output)
        assert json.loads((output / 'sstv-report.json').read_text())['images'] == []
        assert 'Supported image modes' in run(audio, temp / 'bad-mode', 'pd120')
        for case, samples, rate, message in [
            ('stereo', np.zeros((8000, 2)), 8000, 'must be mono'),
            ('rate', np.zeros(4000), 4000, '8..96 kHz'),
            ('duration', np.zeros(8000*361), 8000, 'exceeds 360'),
        ]:
            sf.write(audio, samples, rate)
            assert message in run(audio, temp / case), case
            assert not (temp / case).exists()
        audio.write_bytes(b'not audio')
        assert 'Cannot open' in run(audio, temp / 'broken')
        with audio.open('wb') as stream:
            stream.truncate(128*1024*1024+1)
        assert '<=128 MiB' in run(audio, temp / 'large')
        print('PASS silence and resource/input rejection cases')


if __name__ == '__main__':
    main()
