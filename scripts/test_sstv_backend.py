"""Independent recording gate for the pinned SSTV helper; fixtures stay in build/."""
import gzip
import io
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from PIL import Image
import soundfile as sf


def main():
    root = Path(__file__).resolve().parents[1]
    backend = root / 'build/sstv-backend/release/sdrtown_sstv.exe'
    fixtures = [
        ('robot36', root / 'build/reference-sstv-rust/tests/assets/real_recording.wav.gz',
         root / 'build/reference-sstv-rust/examples/patch.png', 15.0),
        ('martin1', root / 'build/reference-sstv/test/data/m1.ogg',
         root / 'build/reference-sstv/examples/m1.png', None),
    ]
    for mode, recording, expected, threshold in fixtures:
        source = io.BytesIO(gzip.decompress(recording.read_bytes())) if recording.suffix == '.gz' else recording
        samples, rate = sf.read(source, dtype='int16')
        if samples.ndim == 2:
            samples = samples[:, 0]  # Reference recording's first channel, as in VIS regression.
        with tempfile.TemporaryDirectory(prefix='sstv-backend-') as directory:
            temporary = Path(directory)
            pcm = temporary / 'input.pcm'
            pcm.write_bytes(samples.astype('<i2').tobytes())
            result = subprocess.run([str(backend), str(pcm), str(rate), str(temporary / 'images'), mode],
                                    capture_output=True, text=True, check=True, timeout=120)
            records = [json.loads(line) for line in result.stdout.splitlines()]
            assert records and records[0]['complete'], result.stdout
            item = records[0]
            rgb = np.frombuffer((temporary / 'images' / item['file']).read_bytes(), dtype=np.uint8)
            rgb = rgb.reshape(item['height'], item['width'], 3)
            output = root / 'build' / f'sstv-verified-{mode}.png'
            Image.fromarray(rgb).save(output)
            if expected.exists():
                reference = np.asarray(Image.open(expected).convert('RGB'))
                if reference.shape == rgb.shape:
                    mae = float(np.abs(rgb.astype(float) - reference.astype(float)).mean())
                    print(mode, 'RGB MAE', mae, 'reference', expected)
                    if threshold is not None:
                        assert mae < threshold, mae
                else:
                    assert threshold is None, (reference.shape, rgb.shape)
            else:
                assert threshold is None, expected
            print(item, output)


if __name__ == '__main__':
    main()
