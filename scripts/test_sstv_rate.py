"""DEC-0097: exercise C++ converter on independent SSTV recordings."""
import gzip
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from PIL import Image
import soundfile as sf


def decode(helper, pcm, rate, output, mode):
    result = subprocess.run([str(helper), str(pcm), str(rate), str(output), mode],
                            capture_output=True, check=True, timeout=120)
    items = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(items) == 1, items
    item = items[0]
    rgb = np.frombuffer((output / item['file']).read_bytes(), dtype=np.uint8)
    return item, rgb.reshape(item['height'], item['width'], 3).astype(float)


def main():
    root = Path(__file__).resolve().parents[1]
    helper = root / 'build/sstv-backend/release/sdrtown_sstv.exe'
    tests = root / 'build/bin/Release/sdr_town_tests.exe'
    fixtures = (
        ('robot36', root / 'build/reference-sstv-rust/tests/assets/real_recording.wav.gz',
         root / 'build/reference-sstv-rust/examples/patch.png'),
        ('martin1', root / 'build/reference-sstv/test/data/m1.ogg',
         root / 'build/reference-sstv/examples/m1.png'),
    )
    with tempfile.TemporaryDirectory(prefix='sstv-rate-') as directory:
        temporary = Path(directory)
        for mode, path, image in fixtures:
            source = io.BytesIO(gzip.decompress(path.read_bytes())) if path.suffix == '.gz' else path
            samples, rate = sf.read(source, dtype='int16')
            if samples.ndim == 2:
                samples = samples[:, 0]
            reference = np.asarray(Image.open(image).convert('RGB')).astype(float)
            for partial in (False, True):
                label = f'{mode}-{partial}'
                data = samples[:15 * rate] if partial else samples
                wav, pcm, converted = (temporary / (label + suffix) for suffix in ('.wav', '.pcm', '-48k.pcm'))
                sf.write(wav, data, rate, subtype='PCM_16')
                pcm.write_bytes(data.astype('<i2').tobytes())
                env = dict(os.environ, SDR_TOWN_SSTV_RATE_INPUT=str(wav), SDR_TOWN_SSTV_RATE_OUTPUT=str(converted))
                subprocess.run([str(tests), '[sstv-rate-recording]', '--reporter', 'compact'],
                               env=env, capture_output=True, check=True, timeout=120)
                for requested in (mode, 'auto'):
                    original, original_rgb = decode(helper, pcm, rate, temporary / (label + requested + '-original'), requested)
                    result, rgb = decode(helper, converted, 48000, temporary / (label + requested + '-converted'), requested)
                    assert result['complete'] == original['complete'] == (not partial), (original, result)
                    assert result['rows'] == original['rows'], (original, result)
                    if not partial:
                        before = float(np.abs(original_rgb - reference).mean())
                        after = float(np.abs(rgb - reference).mean())
                        print(label, requested, 'reference MAE native', before, 'converted', after)
                        assert after <= before + 1.0, 'conversion exceeded DEC-0097 regression budget'
                    print(label, requested, 'PASS', result['rows'], 'rows')


if __name__ == '__main__':
    main()
