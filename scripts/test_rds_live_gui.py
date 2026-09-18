"""Bounded real-hardware RDS check. Reception failure is not a synthetic pass."""
import argparse
import json
import os
import secrets
import socket
from pathlib import Path
import subprocess
import time
import urllib.request


def run_with_temporary_gain(command, log, env, gain, duration, output):
    """Exercise the existing authenticated loopback API, restoring saved gain."""
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    token = secrets.token_urlsafe(32)
    command.remove("--no-control-server")
    command += ["--control-port", str(port), "--control-token", token, "--control-auth-required"]
    def request(path, body=None):
        req = urllib.request.Request(f"http://127.0.0.1:{port}/v1/{path}",
            data=None if body is None else json.dumps(body).encode(),
            headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=2) as response:
            return json.load(response)
    started = time.monotonic()
    original = None
    with subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env) as proc:
        try:
            deadline = started + 10
            while True:
                if proc.poll() is not None:
                    raise RuntimeError("GUI exited before gain test")
                try:
                    status = request("status")
                    original = status["state"]["rfGainDb"]
                    break
                except (OSError, KeyError):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("GUI control API did not become ready")
                    time.sleep(.2)
            # Let asynchronous hardware open complete before the controlled interval.
            time.sleep(2)
            applied = request("rf-gain", {"rfGainDb": gain})
            assert applied["ok"], applied
            time.sleep(max(0, started + duration / 1000 - 5 - time.monotonic()))
        finally:
            try:
                if original is not None and proc.poll() is None:
                    restored = request("rf-gain", {"rfGainDb": original})
                    assert restored["ok"], restored
                    (output / "gain.json").write_text(json.dumps({"requestedDb": gain,
                        "restoredDb": original, "restored": True}, indent=2), encoding="utf-8")
            finally:
                try:
                    code = proc.wait(timeout=duration / 1000 + 45)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
                    raise
            assert code == 0, code


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    parser.add_argument("--frequency-mhz", type=float, required=True)
    parser.add_argument("--duration-ms", type=int, default=45000)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--expect-pi", type=lambda text: int(text, 0))
    parser.add_argument("--expect-ps")
    parser.add_argument("--parity", action="store_true", help="Compare native and adapted RDS on identical live MPX")
    parser.add_argument("--rf-gain", type=float, help="Temporary device-0 gain; restores previous setting before exit")
    parser.add_argument("--debugger", type=Path, help="Run under CDB and fail on any first-chance access violation")
    parser.add_argument("--output", type=Path, default=Path("build/rds_live_qa"))
    args = parser.parse_args()
    if not 5000 <= args.duration_ms <= 120000:
        parser.error("duration must be 5000..120000 ms")
    if not 65 <= args.frequency_mhz <= 108 or args.device < 0:
        parser.error("select an FM broadcast frequency and non-negative device index")
    if args.rf_gain is not None and (not 0 <= args.rf_gain <= 49.6 or args.device != 0 or args.duration_ms < 30000):
        parser.error("Gain experiment requires device 0, 0..49.6 dB and at least 30 seconds")
    args.output.mkdir(parents=True, exist_ok=True)
    report = args.output.resolve() / "result.json"
    command = [str(args.exe.resolve()), "--no-control-server",
               "--gui-freq", str(args.frequency_mhz), "--gui-device", str(args.device),
               "--gui-workspace", "listening", "--gui-window-size", "1100x780",
               "--gui-self-test", str(report), "--gui-exit-after-ms", str(args.duration_ms)]
    debugger_log = args.output.resolve() / "debugger.log"
    if args.debugger is not None:
        if not args.debugger.is_file():
            parser.error("CDB executable does not exist")
        if debugger_log.exists():
            parser.error("Use a new output directory for a debugger run")
        command = [str(args.debugger.resolve()), "-G", "-y", str(args.exe.resolve().parent), "-logo", str(debugger_log),
                   "-c", 'sxe -c ".ecxr; kv; g" av; g'] + command
    # No allow-multiple: a second instance must not contend for the same tuner.
    started = time.time()
    env = os.environ.copy()
    parity = args.output.resolve() / "parity.jsonl"
    if args.parity:
        if parity.exists():
            parser.error("Use a new output directory for a parity run")
        env["SDR_TOWN_RDS_PARITY_LOG"] = str(parity)
    with (args.output / "run.log").open("w", encoding="utf-8") as log:
        if args.rf_gain is None:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           timeout=args.duration_ms / 1000 + 45, check=True, env=env)
        else:
            run_with_temporary_gain(command, log, env, args.rf_gain, args.duration_ms, args.output)
    if args.debugger is not None:
        trace = debugger_log.read_text(encoding="utf-8", errors="replace")
        assert "Access violation - code c0000005" not in trace, "Native access violation: inspect debugger.log"
    if args.parity:
        reports = [json.loads(line) for line in parity.read_text(encoding="utf-8").splitlines()]
        assert reports and all(r["blocks"] > 0 and r["mismatches"] == 0 and
                               r["resetMismatches"] == 0 and r["adapterFailures"] == 0 for r in reports), reports
        print("PASS same-input live parity:", reports)
    assert report.exists() and report.stat().st_mtime >= started, "No fresh GUI result"
    result = json.loads(report.read_text(encoding="utf-8"))
    assert result["ok"] and not result["errors"], result
    assert result["device"]["streaming"], result["device"]
    assert result["device"]["runtimeState"] == "live hardware", result["device"]
    rds = result["rds"]
    assert result["rdsContract"] == {"id": "rds", "version": 1, "input": "raw-fm-multiplex"}, result
    assert abs(rds["frequencyHz"] - args.frequency_mhz * 1e6) < 1, rds
    assert rds["identified"] and rds["groups"] >= 3, rds
    if args.expect_pi is not None:
        assert rds["pi"] == args.expect_pi, rds
    if args.expect_ps is not None:
        assert rds["ps"].strip() == args.expect_ps, rds
    print("PASS live GUI RDS:", json.dumps(rds))


if __name__ == "__main__":
    main()
