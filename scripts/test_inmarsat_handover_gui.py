"""Exercise the real GUI host through its local API; explicit hardware permission required."""
import argparse
import json
from pathlib import Path
import secrets
import socket
import subprocess
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path("build/bin/Release/SDR_Town.exe"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--cc-mhz", type=float, default=420.350)
    parser.add_argument("--port", type=int, default=18765)
    parser.add_argument("--dry-p25", action="store_true", help="P25 configured without starting hardware")
    parser.add_argument("--watch-test", action="store_true", help="Test live watch edits; restores saved watch settings")
    parser.add_argument("--allow-hardware", action="store_true", help="Permit RF start/retune using saved Inmarsat settings")
    args = parser.parse_args()
    if not args.allow_hardware:
        parser.error("This test starts/retunes an SDR. Supply --allow-hardware explicitly.")
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", args.port))  # Never talk to an existing control server.
    args.output.mkdir(parents=True, exist_ok=True)
    token = secrets.token_hex(24)
    url = f"http://127.0.0.1:{args.port}"

    def request(path, body=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(url + path, data=data, headers={
            "Authorization": "Bearer " + token, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=15) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            return json.load(error)

    command = [str(args.exe.resolve()), "--allow-multiple", "--no-remote-diagnostics",
               "--control-port", str(args.port), "--control-token", token,
               "--gui-device", str(args.device), "--p25-cc", str(args.cc_mhz),
               "--gui-auto-follow",
               "--gui-exit-after-ms", "45000" if args.watch_test else "20000"]
    if args.dry_p25:
        command.append("--gui-dry-run")
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    evidence = {"ok": False, "dryP25": args.dry_p25}
    saved_watch = None
    with (args.output / "gui.log").open("w", encoding="utf-8") as log:
        proc = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, startupinfo=startup)
        try:
            deadline = time.monotonic() + 12
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(f"GUI exited during startup: {proc.returncode}")
                try:
                    before = request("/v1/status")["state"]["p25"]
                    if before["controlFrequencyHz"] == args.cc_mhz * 1e6 and before["autoFollow"]:
                        break
                except (OSError, KeyError):
                    pass
                if time.monotonic() >= deadline:
                    raise TimeoutError("GUI did not arm the requested test CC")
                time.sleep(0.1)
            evidence["before"] = before
            prepare = request("/v1/inmarsat/control", {"action": "prepare"})
            evidence["prepare"] = prepare
            assert prepare.get("requiresP25Stop") and not prepare.get("ready"), prepare
            refused = request("/v1/inmarsat/control", {"action": "start", "force": True})
            evidence["unconfirmed"] = refused
            assert not refused.get("ok") and refused.get("requiresP25Stop"), refused
            unchanged = request("/v1/status")["state"]["p25"]
            assert unchanged["controlFrequencyHz"] == before["controlFrequencyHz"], unchanged
            no_force = request("/v1/inmarsat/control", {"action": "start", "stopP25": True})
            evidence["withoutForce"] = no_force
            assert not no_force.get("ok"), no_force
            assert request("/v1/status")["state"]["p25"]["controlFrequencyHz"] == before["controlFrequencyHz"]
            started = request("/v1/inmarsat/control", {"action": "start", "force": True, "stopP25": True})
            assert started.get("ok"), started
            time.sleep(1)
            live = request("/v1/inmarsat/status")["inmarsat"]
            evidence["live"] = {key: live[key] for key in (
                "state", "streamState", "deviceConnected", "activeDeviceIndex",
                "tunedHz", "rawBlocks", "lastStatus")}
            assert live["deviceConnected"] and live["streamState"] == "live hardware", evidence["live"]
            # rawBlocks counts decoded protocol blocks, not IQ reception. No
            # satellite antenna/traffic is required to prove hardware handover.
            first_samples = live["diagnostics"]["samples"]
            time.sleep(0.5)
            later = request("/v1/inmarsat/status")["inmarsat"]
            evidence["iqSamples"] = [first_samples, later["diagnostics"]["samples"]]
            assert 0 < first_samples < later["diagnostics"]["samples"], evidence["iqSamples"]
            if args.watch_test:
                saved_watch = live["config"]["watch"]
                watch = dict(saved_watch, enabled=True, maxConcurrentChannels=4, channels=[
                    {"id": f"gui-test-{i}", "label": "Test", "frequencyHz": live["tunedHz"] + i * 25000,
                     "rate": 10500, "enabled": True} for i in range(4)])

                def wait_channels(count):
                    deadline = time.monotonic() + 8
                    while time.monotonic() < deadline:
                        state = request("/v1/inmarsat/status")["inmarsat"]
                        current = state["diagnostics"].get("watch", {})
                        channels = current.get("channels", [])
                        if len(channels) == count and all(c["decoder"].get("samples", 0) > 0 for c in channels):
                            return current
                        time.sleep(0.1)
                    raise TimeoutError(f"Watch did not apply {count} channels")

                assert request("/v1/inmarsat/control", {"action": "watch", "watch": watch}).get("ok")
                four = wait_channels(4)
                evidence["fourWorkers"] = {k: four[k] for k in ("workerCount", "maxPendingBlocksPerChannel", "loadRatio")}
                assert four["workerCount"] == 4 and four["maxPendingBlocksPerChannel"] == 1
                invalid = dict(watch, channels=[])
                assert not request("/v1/inmarsat/control", {"action": "watch", "watch": invalid}).get("ok")
                wait_channels(4)
                watch["channels"] = watch["channels"][:2]
                assert request("/v1/inmarsat/control", {"action": "watch", "watch": watch}).get("ok")
                evidence["editedWorkers"] = wait_channels(2)["workerCount"]
                disabled = dict(watch, enabled=False)
                assert request("/v1/inmarsat/control", {"action": "watch", "watch": disabled}).get("ok")
                deadline = time.monotonic() + 8
                while True:
                    current = request("/v1/inmarsat/status")["inmarsat"]
                    if "watch" not in current["diagnostics"] and current["diagnostics"].get("samples", 0) > 0:
                        break
                    assert time.monotonic() < deadline, "Manual decoder did not resume"
                    time.sleep(0.1)
                evidence["manualResumed"] = True
            after = request("/v1/status")["state"]["p25"]
            evidence["after"] = after
            assert after["controlFrequencyHz"] == 0 and not after["autoFollow"], after
            assert not after["followEnabled"] and not after["trafficActive"] and not after["warmStandbyActive"], after
            assert request("/v1/inmarsat/control", {"action": "stop"}).get("ok")
            time.sleep(0.2)
            stopped = request("/v1/status")["state"]["p25"]
            evidence["stopped"] = stopped
            assert stopped["controlFrequencyHz"] == 0 and not stopped["followEnabled"], stopped
            assert request("/v1/inmarsat/control", {"action": "prepare"}).get("ready")
            if saved_watch is not None:
                assert request("/v1/inmarsat/control", {"action": "watch", "watch": saved_watch}).get("ok")
                saved_watch = None
            proc.wait(timeout=50)
            assert proc.returncode == 0, proc.returncode
            evidence["ok"] = True
            print("PASS real GUI host: probe/refusal, confirmed handover, live IQ, no P25 restart")
        finally:
            if proc.poll() is None:
                try:
                    request("/v1/inmarsat/control", {"action": "stop"})
                    if saved_watch is not None:
                        restored = request("/v1/inmarsat/control", {"action": "watch", "watch": saved_watch})
                        evidence["watchRestored"] = bool(restored.get("ok"))
                    proc.wait(timeout=50)
                except (OSError, subprocess.TimeoutExpired):
                    proc.kill()
                    proc.wait(timeout=10)
            (args.output / "result.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
