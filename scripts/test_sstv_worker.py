"""DEC-0098: queue -> C++ converter -> helper -> verified image integration."""
import argparse
import gzip
import io
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np
import soundfile as sf


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core-exe', type=Path, default=root / 'build/bin/Release/sdr_town_tests.exe')
    parser.add_argument('--gui-exe', type=Path, default=root / 'build/bin/Release/sdr_town_workspace_tests.exe')
    args = parser.parse_args()
    core, gui = args.core_exe.resolve(), args.gui_exe.resolve()
    fixtures = (
        ('robot36', root / 'build/reference-sstv-rust/tests/assets/real_recording.wav.gz'),
        ('martin1', root / 'build/reference-sstv/test/data/m1.ogg'),
    )
    with tempfile.TemporaryDirectory(prefix='sstv-worker-') as directory:
        temporary = Path(directory)
        for mode, path in fixtures:
            source = io.BytesIO(gzip.decompress(path.read_bytes())) if path.suffix == '.gz' else path
            samples, rate = sf.read(source, dtype='int16')
            if samples.ndim == 2:
                samples = samples[:, 0]
            for partial in (False, True):
                label = f'{mode}-{partial}'
                data = samples[:15 * rate] if partial else samples
                wav, pcm, reference = (temporary / (label + suffix) for suffix in ('.wav', '.pcm', '-48k.wav'))
                sf.write(wav, data, rate, subtype='PCM_16')
                env = dict(os.environ, SDR_TOWN_SSTV_RATE_INPUT=str(wav), SDR_TOWN_SSTV_RATE_OUTPUT=str(pcm))
                subprocess.run([str(core), '[sstv-rate-recording]', '--reporter', 'compact'],
                               env=env, capture_output=True, check=True, timeout=120)
                sf.write(reference, np.frombuffer(pcm.read_bytes(), dtype='<i2'), 48000, subtype='PCM_16')
                for requested in (mode, 'auto'):
                    env = dict(os.environ, SDR_TOWN_SSTV_STREAM_INPUT=str(wav),
                               SDR_TOWN_SSTV_STREAM_REFERENCE=str(reference), SDR_TOWN_SSTV_STREAM_MODE=requested)
                    env['SDR_TOWN_SSTV_LIVE_SCREENSHOT'] = str(root / f'build/sstv-live-{label}-{requested}.png')
                    for test in ('[sstv-stream-recording]', '[sstv-live-gui-recording]'):
                        run = subprocess.run([str(gui), test], env=env,
                                             capture_output=True, text=True, timeout=40)
                        print(label, requested, test, run.stdout, run.stderr)
                        assert run.returncode == 0, 'stream worker/GUI parity failed'


if __name__ == '__main__':
    main()
