"""DEC-0096: independent file/pipe parity and progressive-before-EOF gate."""
import gzip
import io
import json
from pathlib import Path
import subprocess
import tempfile
import threading

import soundfile as sf


def streamed(command, data, prefix):
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    lines, errors, failures = [], [], []
    first_row, resume = threading.Event(), threading.Event()

    def read_output():
        try:
            total = 0
            for line in process.stdout:
                total += len(line)
                if total > 4 * 1024 * 1024 or len(line) > 4096:
                    raise AssertionError('helper output exceeded transport limits')
                item = json.loads(line)
                lines.append(item)
                if item.get('kind') == 'row':
                    first_row.set()
        except BaseException as error:
            failures.append(error)
            process.kill()

    def read_errors():
        errors.append(process.stderr.read(65537))
        if len(errors[-1]) > 65536:
            failures.append(AssertionError('helper stderr exceeded limit'))
            process.kill()

    def write_input():
        try:
            # Deliberately split i16 samples and cross the helper's read buffer.
            offset, index = 0, 0
            sizes = (1, 3, 257, 8191, 65536)
            for end in (prefix, len(data)):
                while offset < end:
                    count = min(sizes[index % len(sizes)], end - offset)
                    process.stdin.write(data[offset:offset + count])
                    offset += count
                    index += 1
                process.stdin.flush()
                if end == prefix and not resume.wait(20):
                    raise TimeoutError('no progressive output before stdin EOF')
            process.stdin.close()
        except BaseException as error:
            failures.append(error)
            process.kill()

    threads = [threading.Thread(target=target) for target in
               (read_output, read_errors, write_input)]
    for thread in threads:
        thread.start()
    try:
        assert first_row.wait(15), 'decoder buffered the whole input or emitted no row'
        assert process.poll() is None, 'decoder stopped before input EOF'
        resume.set()
        assert process.wait(timeout=120) == 0, 'stream helper failed'
    finally:
        resume.set()
        if process.poll() is None:
            process.kill()
        process.wait(timeout=10)
        for thread in threads:
            thread.join(timeout=10)
            assert not thread.is_alive(), 'pipe worker did not stop'
        for pipe in (process.stdin, process.stdout, process.stderr):
            pipe.close()
    assert not failures, failures
    assert not any(errors), errors
    return lines


def main():
    root = Path(__file__).resolve().parents[1]
    helper = root / 'build/sstv-backend/release/sdrtown_sstv.exe'
    fixtures = (
        ('robot36', root / 'build/reference-sstv-rust/tests/assets/real_recording.wav.gz'),
        ('martin1', root / 'build/reference-sstv/test/data/m1.ogg'),
    )
    with tempfile.TemporaryDirectory(prefix='sstv-stream-') as directory:
        temporary = Path(directory)
        for mode, path in fixtures:
            source = io.BytesIO(gzip.decompress(path.read_bytes())) if path.suffix == '.gz' else path
            samples, rate = sf.read(source, dtype='int16')
            if samples.ndim == 2:
                samples = samples[:, 0]
            for requested in (mode, 'auto'):
                for partial in (False, True):
                    label = f'{mode}-{requested}-{partial}'
                    pcm = samples[:15 * rate] if partial else samples
                    data = pcm.astype('<i2').tobytes()
                    input_file = temporary / (label + '.pcm')
                    input_file.write_bytes(data)
                    file_output = temporary / (label + '-file')
                    pipe_output = temporary / (label + '-pipe')
                    file_run = subprocess.run([str(helper), str(input_file), str(rate),
                                               str(file_output), requested, '--progress'],
                                              capture_output=True, check=True, timeout=120)
                    expected = [json.loads(line) for line in file_run.stdout.splitlines()]
                    actual = streamed([str(helper), '--stdin', str(rate), str(pipe_output),
                                       requested, '--progress'], data, min(len(data), 15 * rate * 2))
                    assert actual == expected, label
                    images = [item for item in actual if 'file' in item]
                    assert images and images[0]['complete'] == (not partial), images
                    for image in images:
                        name = image['file']
                        assert (file_output / name).read_bytes() == (pipe_output / name).read_bytes()
                    print(label, 'PASS: before-EOF rows, metadata and RGB identical')

        for label, data in (('odd', b'\0'), ('over-budget', bytes((8000 * 360 + 1) * 2))):
            result = subprocess.run([str(helper), '--stdin', '8000', str(temporary / label), 'auto'],
                                    input=data, capture_output=True, timeout=120)
            assert result.returncode != 0 and b'SSTV backend error:' in result.stderr, label
            print(label, 'PASS: rejected')
        existing = temporary / 'existing'
        existing.mkdir()
        result = subprocess.run([str(helper), '--stdin', '8000', str(existing), 'auto'],
                                input=b'', capture_output=True, timeout=10)
        assert result.returncode != 0, 'existing output accepted'
        # An idle pipe must be terminable/reaped by its owner, without more input.
        process = subprocess.Popen([str(helper), '--stdin', '8000', str(temporary / 'idle'), 'auto'],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            try:
                process.wait(timeout=0.2)
                raise AssertionError('idle stream exited without EOF')
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=10)
        print('existing output and idle cancellation PASS')


if __name__ == '__main__':
    main()
