#!/usr/bin/env python3
"""Tiny SDR Town remote diagnostics collector.

This server intentionally accepts compact JSON events only. It is not an IQ,
audio, or crash-dump upload service.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


SESSION_RE = re.compile(r"[^A-Za-z0-9_.-]+")


def safe_name(value: Any) -> str:
    text = str(value or "unknown")[:96]
    text = SESSION_RE.sub("_", text).strip("._-")
    return text or "unknown"


class DiagnosticsState:
    def __init__(self, out_dir: Path, token: str | None, max_bytes: int) -> None:
        self.out_dir = out_dir
        self.token = token
        self.max_bytes = max_bytes
        self.count = 0
        self.started = time.time()
        out_dir.mkdir(parents=True, exist_ok=True)

    def write_event(self, event: dict[str, Any]) -> Path:
        session = safe_name(event.get("sessionId"))
        path = self.out_dir / f"{session}.jsonl"
        with path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(event, ensure_ascii=True, separators=(",", ":")))
            f.write("\n")
        self.count += 1
        return path


class Handler(BaseHTTPRequestHandler):
    server_version = "SDRTownDiag/1.0"

    @property
    def state(self) -> DiagnosticsState:
        return self.server.state  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: Any) -> None:
        return

    def _send_json(self, code: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _authorized(self) -> bool:
        if not self.state.token:
            return True
        expected = f"Bearer {self.state.token}"
        return self.headers.get("Authorization", "") == expected

    def do_GET(self) -> None:
        if self.path.rstrip("/") == "/health":
            self._send_json(200, {
                "ok": True,
                "events": self.state.count,
                "uptimeSeconds": round(time.time() - self.state.started, 2),
            })
        else:
            self._send_json(404, {"ok": False, "error": "not found"})

    def do_POST(self) -> None:
        if not self._authorized():
            self._send_json(401, {"ok": False, "error": "unauthorized"})
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._send_json(400, {"ok": False, "error": "bad content length"})
            return

        if length <= 0:
            self._send_json(400, {"ok": False, "error": "empty request"})
            return
        if length > self.state.max_bytes:
            self._send_json(413, {"ok": False, "error": "payload too large"})
            return

        raw = self.rfile.read(length)
        try:
            event = json.loads(raw.decode("utf-8"))
        except Exception as exc:
            self._send_json(400, {"ok": False, "error": f"bad json: {exc}"})
            return
        if not isinstance(event, dict):
            self._send_json(400, {"ok": False, "error": "root must be an object"})
            return

        path = self.state.write_event(event)
        payload = event.get("payload") if isinstance(event.get("payload"), dict) else {}
        p25 = ""
        if isinstance(payload, dict):
            tg = payload.get("talkgroup") or payload.get("talkgroupId")
            diag = payload.get("diag")
            gate = payload.get("speakerGate")
            if tg or diag or gate:
                p25 = f" tg={tg} diag={diag} gate={gate}"
        print(
            f"{time.strftime('%H:%M:%S')} {event.get('type','?')} "
            f"{event.get('severity','?')} seq={event.get('seq','?')} "
            f"bytes={length}{p25} -> {path.name}",
            flush=True,
        )
        self._send_json(202, {"ok": True})


class DiagnosticsServer(ThreadingHTTPServer):
    def __init__(self, addr: tuple[str, int], state: DiagnosticsState) -> None:
        super().__init__(addr, Handler)
        self.state = state


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Collect compact SDR Town remote diagnostics JSONL events.")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host, use 0.0.0.0 for LAN/VPS testing")
    parser.add_argument("--port", type=int, default=8787, help="Bind port")
    parser.add_argument("--token", default=None, help="Optional bearer token required from clients")
    parser.add_argument("--out", default="remote_diagnostics", help="Output directory for session JSONL files")
    parser.add_argument("--max-bytes", type=int, default=65536, help="Maximum accepted request bytes")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = DiagnosticsState(Path(args.out), args.token, max(2048, args.max_bytes))
    server = DiagnosticsServer((args.host, args.port), state)
    print(f"SDR Town diagnostics server listening on http://{args.host}:{args.port}/ingest")
    print(f"Writing JSONL sessions to {state.out_dir.resolve()}")
    if args.token:
        print("Bearer token required.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping diagnostics server.", file=sys.stderr)
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
