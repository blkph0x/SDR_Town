#!/usr/bin/env python3
"""Replay a short slice from a saved IQ capture via SDR Town --cli and summarize metrics."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def latest_capture_dir(root: Path) -> Path | None:
    base = Path.home() / 'AppData/Roaming/SDR_Town/SDR Town/iq_test_captures'
    if not base.is_dir():
        return None
    dirs = sorted([p for p in base.iterdir() if p.is_dir()], key=lambda p: p.stat().st_mtime, reverse=True)
    return dirs[0] if dirs else None


def run_cli_replay(exe: Path, command: str, timeout_s: float) -> tuple[int, str]:
    proc = subprocess.run(
        [str(exe), '--cli', '--allow-multiple', '--cmd', command],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout_s,
        check=False,
    )
    return proc.returncode, proc.stdout or ''


def main() -> int:
    parser = argparse.ArgumentParser(description='Replay P25 capture slice for audit')
    parser.add_argument('--exe', default='build/bin/Release/SDR_Town.exe')
    parser.add_argument('--capture-dir', default='')
    parser.add_argument('--target-mhz', type=float, default=417.675)
    parser.add_argument('--center-mhz', type=float, default=417.675)
    parser.add_argument('--skip-ms', type=float, default=231000)
    parser.add_argument('--ms', type=float, default=8000)
    parser.add_argument('--timeout', type=float, default=180.0)
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[2]
    exe = (repo / args.exe).resolve()
    if not exe.is_file():
        print(f'Executable not found: {exe}', file=sys.stderr)
        return 2

    cap_dir = Path(args.capture_dir) if args.capture_dir else latest_capture_dir(repo)
    if not cap_dir or not cap_dir.is_dir():
        print('No capture directory found', file=sys.stderr)
        return 2
    meta = next(cap_dir.glob('*.sigmf-meta'), None)
    if meta is None:
        print(f'No sigmf-meta in {cap_dir}', file=sys.stderr)
        return 2

    # CLI mode: pipe commands to interactive shell; do not pass p25 replay as argv.
    meta_path = str(meta).replace('\\', '/')
    command = (
        f'p25 replay "{meta_path}" {args.target_mhz} {int(args.ms)} phase2 '
        f'skip={int(args.skip_ms)} center={args.center_mhz} '
        f'nac=0x2dc wacn=0xbee00 system=0x2d1 trace'
    )
    print('CLI command:', command)
    rc, out = run_cli_replay(exe, command, args.timeout)
    print(out)
    p2bursts = [int(x) for x in re.findall(r'p2bursts=(\d+)', out)]
    p2vcw = [int(x) for x in re.findall(r'p2vcw=(\d+)', out)]
    p2sf = [int(x) for x in re.findall(r'p2sf=(\d+)', out)]
    best_bursts = max(p2bursts) if p2bursts else 0
    best_vcw = max(p2vcw) if p2vcw else 0
    best_sf = max(p2sf) if p2sf else 0
    print(
        f'AUDIT: capture={cap_dir.name} best_p2bursts={best_bursts} '
        f'best_p2vcw={best_vcw} best_p2sf={best_sf} cli_exit={rc}'
    )
    return 0 if best_bursts > 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
