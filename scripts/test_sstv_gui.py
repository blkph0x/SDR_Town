"""Exercise the real SSTV window/worker and decoder against independent recordings."""
import gzip
import io
import os
from pathlib import Path
import subprocess
import tempfile

import soundfile as sf


def main():
    root = Path(__file__).resolve().parents[1]
    executable = root / 'build/bin/Release/sdr_town_workspace_tests.exe'
    for mode, path in [
        ('robot36', root / 'build/reference-sstv-rust/tests/assets/real_recording.wav.gz'),
        ('martin1', root / 'build/reference-sstv/test/data/m1.ogg'),
    ]:
        source = io.BytesIO(gzip.decompress(path.read_bytes())) if path.suffix == '.gz' else path
        samples, rate = sf.read(source, dtype='int16')
        if samples.ndim == 2:
            samples = samples[:, 0]
        with tempfile.TemporaryDirectory(prefix='sstv gui ') as temporary:
            audio = Path(temporary) / 'recording.wav'
            sf.write(audio, samples, rate, subtype='PCM_16')
            environment = dict(os.environ, SDR_TOWN_SSTV_GUI_FIXTURE=str(audio),
                               SDR_TOWN_SSTV_GUI_SCREENSHOT=str(root / f'build/sstv-gui-{mode}.png'))
            result = subprocess.run([str(executable), '[sstv-gui-recording]'], env=environment,
                                    capture_output=True, text=True, timeout=150)
            print(mode, result.stdout, result.stderr)
            assert result.returncode == 0
            assert 'All tests passed' in result.stdout


if __name__ == '__main__':
    main()
