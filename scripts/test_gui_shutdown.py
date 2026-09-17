"""Bounded, debugger-backed real RX startup/shutdown regression (DEC-0088/89)."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    p.add_argument("--debugger", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--cycles", type=int, default=5)
    args = p.parse_args()
    if not 1 <= args.cycles <= 10 or not args.exe.is_file() or not args.debugger.is_file():
        p.error("Require executable, CDB, and 1..10 cycles")
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for cycle in range(args.cycles):
        stem = args.output.resolve() / str(cycle)
        report, trace = stem.with_suffix('.json'), stem.with_suffix('.debugger.log')
        command = [str(args.debugger.resolve()), '-G', '-y', str(args.exe.resolve().parent),
                   '-logo', str(trace), '-c', 'sxe -c ".ecxr; kv; g" av; g',
                   str(args.exe.resolve()), '--no-control-server', '--gui-freq', '98.1',
                   '--gui-device', '0', '--gui-self-test', str(report), '--gui-exit-after-ms', '12000']
        with stem.with_suffix('.run.log').open('w', encoding='utf-8') as log:
            run = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=90)
        assert run.returncode == 0, (cycle, run.returncode)
        state = json.loads(report.read_text(encoding='utf-8'))
        assert state['ok'] and not state['errors'], state
        assert state['device']['runtimeState'] == 'live hardware', state['device']
        text = trace.read_text(encoding='utf-8', errors='replace')
        console = stem.with_suffix('.run.log').read_text(encoding='utf-8', errors='replace')
        assert 'Access violation - code c0000005' not in text, trace
        assert 'recoverable' not in console.lower(), stem
        assert 'AudioEngine destroyed' in console and 'All streams stopped on shutdown' in console, stem
        results.append({'cycle': cycle, 'hardware': True, 'nativeAccessViolations': 0, 'exit': run.returncode})
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        print(f'PASS cycle {cycle + 1}: real RX, clean native teardown and audio destruction', flush=True)


if __name__ == '__main__':
    main()
