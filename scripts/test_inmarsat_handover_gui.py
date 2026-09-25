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
               "--gui-exit-after-ms", "20000"]
    if args.dry_p25:
        command.append("--gui-dry-run")
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    evidence = {"ok": False, "dryP25": args.dry_p25}
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
            proc.wait(timeout=25)
            assert proc.returncode == 0, proc.returncode
            evidence["ok"] = True
            print("PASS real GUI host: probe/refusal, confirmed handover, live IQ, no P25 restart")
        finally:
            if proc.poll() is None:
                try:
                    request("/v1/inmarsat/control", {"action": "stop"})
                    proc.wait(timeout=25)
                except (OSError, subprocess.TimeoutExpired):
                    proc.kill()
                    proc.wait(timeout=10)
            (args.output / "result.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
