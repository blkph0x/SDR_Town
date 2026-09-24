"""Independent Aero recording parity gate. PCM/framing evidence, not speech acceptance."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--iq', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--speaker', action='store_true', help='Play real-time GUI pass on default device')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    results = []
    for gui, fast in [(False, True), (True, True), (False, False), (True, False)]:
        name = ('gui' if gui else 'cli') + ('-fast' if fast else '-paced')
        wav = (a.out / (name + '.wav')).resolve()
        result = (a.out / (name + '.json')).resolve()
        args = [str(a.exe.resolve()), '--allow-multiple', '--no-remote-diagnostics',
                '--inmarsat-iq', str(a.iq.resolve()), '--inmarsat-mode', '8400',
                '--inmarsat-wav', str(wav), '--inmarsat-result', str(result),
                '--inmarsat-log-dir', str(a.out.resolve()), '--inmarsat-exit-complete']
        if not gui:
            args += ['--cli']
        if fast:
            args += ['--inmarsat-fast']
        if gui and not fast and a.speaker:
            args += ['--inmarsat-play-audio']
        run = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        (a.out / (name + '.log')).write_bytes(run.stdout)
        assert run.returncode == 0, (name, run.returncode)
        report = json.loads(result.read_text(encoding='utf-8-sig'))
        assert report['state'] == 'complete' and not report['error'] and not report['logError'], report
        assert report['validatedFrames'] > 0 and report['voiceFrames'] > 0
        assert report['pcmSamples'] == report['voiceFrames'] * 160
        assert report['audio']['wavSamples'] == report['pcmSamples']
        assert not report['audio']['speakerError'] and report['audio']['speakerDropped'] == 0
        sha = hashlib.sha256(wav.read_bytes()).hexdigest()
        keys = ('samples', 'validatedFrames', 'voiceFrames', 'pcmSamples', 'crcFailed',
                'codecCorrections', 'codecRepeats', 'codecMutes', 'rejectedCFrames')
        evidence = {key: report[key] for key in keys}
        evidence['wavSha256'] = sha
        results.append(evidence)
        assert evidence == results[0], (name, evidence, results[0])
        print(name, 'PASS', evidence, flush=True)
    (a.out / 'parity.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('PASS identical GUI/CLI fast/paced framed PCM; intelligibility needs listening/reference transcript.')


if __name__ == '__main__':
    main()
