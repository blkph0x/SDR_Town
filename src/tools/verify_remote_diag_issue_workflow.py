#!/usr/bin/env python3
"""Smoke-test SDR Town remote diagnostics issue workflow.

This spins the collector in-process, posts reports from multiple fake clients
and hardware hashes, marks one issue fixed, then verifies client status returns
the right per-install issue list and update notice.
"""

from __future__ import annotations

import json
import tempfile
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

import remote_diag_server


def request_json(method: str, url: str, token: str, payload: dict | None = None) -> dict:
    data = None
    headers = {"Authorization": f"Bearer {token}"}
    if payload is not None:
        data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=5) as res:
        return json.loads(res.read().decode("utf-8"))


def make_event(client_id: str, hardware_hash: str, report_id: str, title: str) -> dict:
    return {
        "schema": "sdr-town-remote-diagnostics-v1",
        "app": "SDR_Town",
        "version": "0.2.32",
        "mode": "gui",
        "sessionId": f"sess-{client_id}",
        "clientId": client_id,
        "installId": client_id,
        "hardwareHash": hardware_hash,
        "seq": "1",
        "timeUtc": "2026-07-15T00:00:00.000Z",
        "type": "user.report",
        "severity": "warn",
        "payload": {
            "userReportId": report_id,
            "title": title,
            "area": "P25 audio",
            "details": "Short tester report with capped snippets.",
            "attachment": {
                "name": "short.sigmf-data",
                "size": 1048576,
                "sha256": "abc123",
                "contentEncoding": "base64-prefix",
                "content": "AAECAwQFBgc=",
                "truncated": True,
            },
        },
    }


def main() -> int:
    token = "client-token"
    admin_token = "admin-token"
    with tempfile.TemporaryDirectory(prefix="sdr-town-diag-test-") as tmp:
        state = remote_diag_server.DiagnosticsState(Path(tmp), token, admin_token, 65536)
        server = remote_diag_server.DiagnosticsServer(("127.0.0.1", 0), state)
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{port}"
        try:
            for event in [
                make_event("client-alpha", "hw-alpha", "RPT-alpha-1", "P25 audio is silent"),
                make_event("client-beta", "hw-beta", "RPT-beta-1", "P25 audio is blocky"),
            ]:
                result = request_json("POST", f"{base}/ingest", token, event)
                assert result["ok"] is True

            status_alpha = request_json("GET", f"{base}/client-status?clientId=client-alpha&version=0.2.32", token)
            status_beta = request_json("GET", f"{base}/client-status?clientId=client-beta&version=0.2.32", token)
            assert len(status_alpha["issues"]) == 1
            assert len(status_beta["issues"]) == 1
            assert status_alpha["issues"][0]["title"].endswith("P25 audio is silent")
            assert status_beta["issues"][0]["title"].endswith("P25 audio is blocky")

            issue_id = status_alpha["issues"][0]["issue_id"]
            form = f"token={admin_token}&issue_id={issue_id}&status=fixed&fixed_version=0.2.33&fix_note=smoke+fixed".encode()
            req = urllib.request.Request(
                f"{base}/admin/issue",
                data=form,
                headers={"Content-Type": "application/x-www-form-urlencoded"},
                method="POST",
            )
            with urllib.request.urlopen(req, timeout=5) as res:
                assert json.loads(res.read().decode("utf-8"))["ok"] is True

            status_alpha_old = request_json("GET", f"{base}/client-status?clientId=client-alpha&version=0.2.32", token)
            status_beta_old = request_json("GET", f"{base}/client-status?clientId=client-beta&version=0.2.32", token)
            assert status_alpha_old["bugFixUpdateAvailable"] is True
            assert status_alpha_old["recommendedVersion"] == "0.2.33"
            assert status_beta_old["bugFixUpdateAvailable"] is False

            health = request_json("GET", f"{base}/health", token)
            assert health["issues"]["fixed"] == 1
            assert health["issues"]["outstanding"] == 1
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
            time.sleep(0.05)
    print("Remote diagnostics issue workflow smoke test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
