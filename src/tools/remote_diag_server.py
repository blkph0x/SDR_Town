#!/usr/bin/env python3
"""SDR Town remote diagnostics collector with lightweight issue tracking.

The collector accepts compact JSON events only. It is not an IQ, audio, or
crash-dump upload service. Raw events are kept as JSONL, while warning/error
events are grouped into issues in SQLite so fixes can be marked and reported
back to the affected installation on the next app start.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import sqlite3
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, quote, unquote, urlparse


SESSION_RE = re.compile(r"[^A-Za-z0-9_.-]+")
LONG_HEX_RE = re.compile(r"\b[0-9a-fA-F]{8,}\b")
LONG_NUM_RE = re.compile(r"\b\d{5,}\b")
UUID_RE = re.compile(r"\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b")
ISSUE_STATUSES = {"outstanding", "fixed", "unrequired"}
ISSUE_SEVERITIES = {"warn", "warning", "error", "critical", "fatal"}


def utc_now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def safe_name(value: Any) -> str:
    text = str(value or "unknown")[:96]
    text = SESSION_RE.sub("_", text).strip("._-")
    return text or "unknown"


def digest_text(text: str, length: int = 16) -> str:
    return hashlib.sha256(text.encode("utf-8", errors="replace")).hexdigest()[:length]


def normalize_text(value: Any) -> str:
    text = str(value or "").strip().replace("\\", "/")
    text = UUID_RE.sub("<uuid>", text)
    text = LONG_HEX_RE.sub("<hex>", text)
    text = LONG_NUM_RE.sub("<num>", text)
    text = re.sub(r"\s+", " ", text)
    return text[:500]


def parse_version(value: str) -> tuple[int, ...]:
    text = str(value or "").strip().lstrip("vV")
    parts: list[int] = []
    for part in re.split(r"[^0-9]+", text):
        if part == "":
            continue
        try:
            parts.append(int(part))
        except ValueError:
            parts.append(0)
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts)


def version_gt(candidate: str, current: str) -> bool:
    return parse_version(candidate) > parse_version(current)


def compact_json(value: Any, limit: int = 32768) -> str:
    try:
        text = json.dumps(value, ensure_ascii=True, separators=(",", ":"))
    except Exception:
        text = str(value)
    return text[:limit]


def client_token_from_header(headers: Any) -> str:
    auth = headers.get("Authorization", "")
    if auth.startswith("Bearer "):
        return auth[7:]
    return ""


class DiagnosticsState:
    def __init__(self, out_dir: Path, token: str | None, admin_token: str | None, max_bytes: int) -> None:
        self.out_dir = out_dir
        self.token = token
        self.admin_token = admin_token
        self.max_bytes = max_bytes
        self.count = 0
        self.started = time.time()
        self.lock = threading.Lock()
        self.db_path = out_dir / "diagnostics.sqlite3"
        out_dir.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _connect(self) -> sqlite3.Connection:
        con = sqlite3.connect(self.db_path)
        con.row_factory = sqlite3.Row
        return con

    def _init_db(self) -> None:
        with self.lock, self._connect() as con:
            con.executescript(
                """
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS clients (
                    client_id TEXT PRIMARY KEY,
                    first_seen TEXT NOT NULL,
                    last_seen TEXT NOT NULL,
                    app_version TEXT,
                    mode TEXT,
                    latest_session_id TEXT,
                    hardware_hash TEXT,
                    report_count INTEGER NOT NULL DEFAULT 0
                );
                CREATE TABLE IF NOT EXISTS issues (
                    issue_id TEXT PRIMARY KEY,
                    fingerprint TEXT NOT NULL UNIQUE,
                    title TEXT NOT NULL,
                    event_type TEXT,
                    severity TEXT,
                    first_seen TEXT NOT NULL,
                    last_seen TEXT NOT NULL,
                    first_version TEXT,
                    last_version TEXT,
                    status TEXT NOT NULL DEFAULT 'outstanding',
                    fixed_version TEXT,
                    fix_note TEXT,
                    report_count INTEGER NOT NULL DEFAULT 0,
                    last_payload TEXT
                );
                CREATE TABLE IF NOT EXISTS issue_reports (
                    report_id TEXT PRIMARY KEY,
                    issue_id TEXT NOT NULL,
                    client_id TEXT NOT NULL,
                    session_id TEXT,
                    first_seen TEXT NOT NULL,
                    last_seen TEXT NOT NULL,
                    count INTEGER NOT NULL DEFAULT 0,
                    last_version TEXT,
                    FOREIGN KEY(issue_id) REFERENCES issues(issue_id)
                );
                CREATE INDEX IF NOT EXISTS idx_issue_reports_client ON issue_reports(client_id);
                CREATE INDEX IF NOT EXISTS idx_issues_status ON issues(status);
                """
            )

    def write_event(self, event: dict[str, Any]) -> Path:
        session = safe_name(event.get("sessionId"))
        path = self.out_dir / f"{session}.jsonl"
        with path.open("a", encoding="utf-8") as f:
            f.write(json.dumps(event, ensure_ascii=True, separators=(",", ":")))
            f.write("\n")
        with self.lock:
            self.count += 1
        self.record_event(event)
        return path

    def record_event(self, event: dict[str, Any]) -> None:
        client_id = safe_name(event.get("clientId") or event.get("installId"))
        if client_id == "unknown":
            return

        now = str(event.get("timeUtc") or utc_now())
        version = str(event.get("version") or "")
        mode = str(event.get("mode") or "")
        session_id = str(event.get("sessionId") or "")
        hardware_hash = str(event.get("hardwareHash") or "")
        payload = event.get("payload") if isinstance(event.get("payload"), dict) else {}

        with self.lock, self._connect() as con:
            con.execute(
                """
                INSERT INTO clients(client_id, first_seen, last_seen, app_version, mode, latest_session_id, hardware_hash, report_count)
                VALUES(?, ?, ?, ?, ?, ?, ?, 1)
                ON CONFLICT(client_id) DO UPDATE SET
                    last_seen=excluded.last_seen,
                    app_version=excluded.app_version,
                    mode=excluded.mode,
                    latest_session_id=excluded.latest_session_id,
                    hardware_hash=COALESCE(NULLIF(excluded.hardware_hash, ''), clients.hardware_hash),
                    report_count=clients.report_count + 1
                """,
                (client_id, now, now, version, mode, session_id, hardware_hash),
            )

            if not self._is_issue_event(event):
                return

            issue_key, title = self._issue_key_and_title(event, payload)
            fingerprint = hashlib.sha256(issue_key.encode("utf-8", errors="replace")).hexdigest()
            issue_id = "ISS-" + fingerprint[:12].upper()
            report_id = digest_text(f"{issue_id}|{client_id}", 24)
            event_type = str(event.get("type") or "")
            severity = str(event.get("severity") or "")
            payload_text = compact_json(payload)

            con.execute(
                """
                INSERT INTO issues(issue_id, fingerprint, title, event_type, severity, first_seen, last_seen,
                                   first_version, last_version, status, report_count, last_payload)
                VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, 'outstanding', 1, ?)
                ON CONFLICT(fingerprint) DO UPDATE SET
                    last_seen=excluded.last_seen,
                    last_version=excluded.last_version,
                    report_count=issues.report_count + 1,
                    last_payload=excluded.last_payload
                """,
                (issue_id, fingerprint, title, event_type, severity, now, now, version, version, payload_text),
            )
            con.execute(
                """
                INSERT INTO issue_reports(report_id, issue_id, client_id, session_id, first_seen, last_seen, count, last_version)
                VALUES(?, ?, ?, ?, ?, ?, 1, ?)
                ON CONFLICT(report_id) DO UPDATE SET
                    session_id=excluded.session_id,
                    last_seen=excluded.last_seen,
                    count=issue_reports.count + 1,
                    last_version=excluded.last_version
                """,
                (report_id, issue_id, client_id, session_id, now, now, version),
            )

    def _is_issue_event(self, event: dict[str, Any]) -> bool:
        severity = str(event.get("severity") or "").lower()
        event_type = str(event.get("type") or "").lower()
        if severity in ISSUE_SEVERITIES:
            return True
        if event_type in {
            "app.exception",
            "app.crash",
            "app.problem",
            "diagnostics.problem",
            "user.report",
            "hardware.open",
            "hardware.problem",
            "app.performance.ui_stall",
            "app.performance.resource_pressure",
        }:
            return True
        return False

    def _issue_key_and_title(self, event: dict[str, Any], payload: dict[str, Any]) -> tuple[str, str]:
        event_type = normalize_text(event.get("type"))
        severity = normalize_text(event.get("severity")).lower()
        if event_type == "user.report":
            report_id = normalize_text(payload.get("userReportId"))
            title = normalize_text(payload.get("title") or payload.get("summary") or "User submitted issue")
            area = normalize_text(payload.get("area"))
            key = "|".join([event_type, report_id, area, title])
            label = " - ".join(p for p in [event_type, area, title] if p)
            return key, label[:180] or "User submitted issue"

        stage = normalize_text(payload.get("stage"))
        if event_type == "app.performance.resource_pressure":
            system = payload.get("system") if isinstance(payload.get("system"), dict) else {}
            pressure_summary = normalize_text(
                payload.get("pressureSummary")
                or system.get("pressureSummary")
                or ",".join(str(x) for x in system.get("pressureReasons", []) if x)
            )
            key = "|".join([event_type, severity, stage, pressure_summary])
            label = " - ".join(
                p for p in [event_type, stage, pressure_summary or "resource pressure"] if p
            )
            return key, label[:180] or "Remote diagnostic issue"

        exception_type = normalize_text(payload.get("exceptionType"))
        message = normalize_text(payload.get("message") or payload.get("error") or payload.get("line"))
        diag = normalize_text(payload.get("diag"))
        gate = normalize_text(payload.get("speakerGate") or payload.get("gate"))
        key = "|".join([event_type, severity, stage, exception_type, message, diag, gate])

        title_parts = [event_type]
        if stage:
            title_parts.append(stage)
        if message:
            title_parts.append(message[:120])
        elif diag or gate:
            title_parts.append(f"diag={diag} gate={gate}".strip())
        title = " - ".join(p for p in title_parts if p)[:180] or "Remote diagnostic issue"
        return key, title

    def issue_summary(self) -> dict[str, int]:
        with self.lock, self._connect() as con:
            rows = con.execute("SELECT status, COUNT(*) AS n FROM issues GROUP BY status").fetchall()
        out = {"outstanding": 0, "fixed": 0, "unrequired": 0}
        for row in rows:
            out[str(row["status"])] = int(row["n"])
        return out

    def list_issues(self, limit: int = 250) -> list[dict[str, Any]]:
        with self.lock, self._connect() as con:
            rows = con.execute(
                """
                SELECT issue_id, title, event_type, severity, first_seen, last_seen, first_version, last_version,
                       status, fixed_version, fix_note, report_count, last_payload
                FROM issues
                ORDER BY
                    CASE status WHEN 'outstanding' THEN 0 WHEN 'fixed' THEN 1 ELSE 2 END,
                    last_seen DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        return [dict(row) for row in rows]

    def list_clients(self, limit: int = 100) -> list[dict[str, Any]]:
        with self.lock, self._connect() as con:
            rows = con.execute(
                """
                SELECT client_id, first_seen, last_seen, app_version, mode, latest_session_id, hardware_hash, report_count
                FROM clients
                ORDER BY last_seen DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        return [dict(row) for row in rows]

    def update_issue(self, issue_id: str, status: str, fixed_version: str, fix_note: str) -> bool:
        status = status if status in ISSUE_STATUSES else "outstanding"
        issue_id = issue_id.strip()
        fixed_version = fixed_version.strip()
        fix_note = fix_note.strip()[:1000]
        with self.lock, self._connect() as con:
            cur = con.execute(
                "UPDATE issues SET status=?, fixed_version=?, fix_note=? WHERE issue_id=?",
                (status, fixed_version, fix_note, issue_id),
            )
            return cur.rowcount > 0

    def client_status(self, client_id: str, current_version: str) -> dict[str, Any]:
        client_id = safe_name(client_id)
        with self.lock, self._connect() as con:
            rows = con.execute(
                """
                SELECT i.issue_id, i.title, i.fixed_version, i.fix_note, i.status, r.count, r.last_seen
                FROM issues i
                JOIN issue_reports r ON r.issue_id = i.issue_id
                WHERE r.client_id=? AND i.status='fixed' AND COALESCE(i.fixed_version, '') != ''
                ORDER BY i.fixed_version DESC, i.last_seen DESC
                """,
                (client_id,),
            ).fetchall()
            outstanding = con.execute(
                """
                SELECT COUNT(*) AS n
                FROM issues i
                JOIN issue_reports r ON r.issue_id = i.issue_id
                WHERE r.client_id=? AND i.status='outstanding'
                """,
                (client_id,),
            ).fetchone()
            issue_rows = con.execute(
                """
                SELECT i.issue_id, i.title, i.event_type, i.severity, i.status, i.fixed_version,
                       i.fix_note, i.first_seen, i.last_seen, i.first_version, i.last_version,
                       i.report_count, r.count AS client_report_count, r.last_seen AS client_last_seen
                FROM issues i
                JOIN issue_reports r ON r.issue_id = i.issue_id
                WHERE r.client_id=?
                ORDER BY r.last_seen DESC
                LIMIT 100
                """,
                (client_id,),
            ).fetchall()

        fixed: list[dict[str, Any]] = []
        recommended = ""
        for row in rows:
            fv = str(row["fixed_version"] or "")
            if not version_gt(fv, current_version):
                continue
            fixed.append(dict(row))
            if not recommended or version_gt(fv, recommended):
                recommended = fv
        return {
            "ok": True,
            "clientId": client_id,
            "currentVersion": current_version,
            "bugFixUpdateAvailable": bool(fixed),
            "recommendedVersion": recommended,
            "fixedIssues": fixed[:20],
            "issues": [dict(row) for row in issue_rows],
            "outstandingIssueCount": int(outstanding["n"] if outstanding else 0),
        }


class Handler(BaseHTTPRequestHandler):
    server_version = "SDRTownDiag/2.0"

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
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_html(self, code: int, html_text: str) -> None:
        body = html_text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _query(self) -> dict[str, list[str]]:
        return parse_qs(urlparse(self.path).query)

    def _client_authorized(self) -> bool:
        if not self.state.token:
            return True
        return client_token_from_header(self.headers) == self.state.token

    def _admin_authorized(self, values: dict[str, list[str]] | None = None) -> bool:
        token = self.state.admin_token or self.state.token
        if not token:
            return True
        if client_token_from_header(self.headers) == token:
            return True
        values = values if values is not None else self._query()
        supplied = values.get("token", [""])[0]
        return supplied == token

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = parse_qs(parsed.query)

        if path == "/health":
            self._send_json(200, {
                "ok": True,
                "events": self.state.count,
                "uptimeSeconds": round(time.time() - self.state.started, 2),
                "issues": self.state.issue_summary(),
            })
            return

        if path == "/client-status":
            if not self._client_authorized():
                self._send_json(401, {"ok": False, "error": "unauthorized"})
                return
            client_id = query.get("clientId", [""])[0]
            version = query.get("version", [""])[0]
            if not client_id:
                self._send_json(400, {"ok": False, "error": "missing clientId"})
                return
            self._send_json(200, self.state.client_status(client_id, version))
            return

        if path == "/api/issues":
            if not self._admin_authorized(query):
                self._send_json(401, {"ok": False, "error": "admin unauthorized"})
                return
            self._send_json(200, {"ok": True, "issues": self.state.list_issues()})
            return

        if path == "/api/clients":
            if not self._admin_authorized(query):
                self._send_json(401, {"ok": False, "error": "admin unauthorized"})
                return
            self._send_json(200, {"ok": True, "clients": self.state.list_clients()})
            return

        if path == "/admin":
            if not self._admin_authorized(query):
                self._send_html(401, self._admin_login())
                return
            self._send_html(200, self._admin_page(query.get("token", [""])[0]))
            return

        self._send_json(404, {"ok": False, "error": "not found"})

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"

        if path == "/admin/issue":
            length = int(self.headers.get("Content-Length", "0") or "0")
            body = self.rfile.read(max(0, min(length, 64 * 1024))).decode("utf-8", errors="replace")
            form = parse_qs(body)
            if not self._admin_authorized(form):
                self._send_json(401, {"ok": False, "error": "admin unauthorized"})
                return
            ok = self.state.update_issue(
                form.get("issue_id", [""])[0],
                form.get("status", ["outstanding"])[0],
                form.get("fixed_version", [""])[0],
                form.get("fix_note", [""])[0],
            )
            if "text/html" in self.headers.get("Accept", ""):
                token = quote(form.get("token", [""])[0])
                self.send_response(303)
                self.send_header("Location", f"/admin?token={token}")
                self.end_headers()
            else:
                self._send_json(200 if ok else 404, {"ok": ok})
            return

        if path != "/ingest":
            self._send_json(404, {"ok": False, "error": "not found"})
            return

        if not self._client_authorized():
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

        path_written = self.state.write_event(event)
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
            f"bytes={length}{p25} -> {path_written.name}",
            flush=True,
        )
        self._send_json(202, {"ok": True})

    def _admin_login(self) -> str:
        return """<!doctype html>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SDR Town Diagnostics Admin</title>
<h1>SDR Town Diagnostics Admin</h1>
<form method="get" action="/admin">
  <label>Admin token <input name="token" type="password" autofocus></label>
  <button type="submit">Open</button>
</form>"""

    def _admin_page(self, token: str) -> str:
        issues = self.state.list_issues()
        clients = self.state.list_clients(25)
        summary = self.state.issue_summary()
        token_q = quote(token)
        issue_rows = []
        for issue in issues:
            issue_id = html.escape(str(issue["issue_id"]))
            status = str(issue.get("status") or "outstanding")
            fixed_version = html.escape(str(issue.get("fixed_version") or ""))
            fix_note = html.escape(str(issue.get("fix_note") or ""))
            payload_preview = html.escape(str(issue.get("last_payload") or "")[:2500])
            if payload_preview:
                payload_preview = f"<details><summary>payload</summary><pre>{payload_preview}</pre></details>"
            options = "".join(
                f'<option value="{s}" {"selected" if s == status else ""}>{s}</option>'
                for s in sorted(ISSUE_STATUSES)
            )
            issue_rows.append(f"""
<tr>
  <td><code>{issue_id}</code><br><small>{html.escape(str(issue.get('event_type') or ''))} / {html.escape(str(issue.get('severity') or ''))}</small></td>
  <td>{html.escape(str(issue.get('title') or ''))}<br><small>first {html.escape(str(issue.get('first_seen') or ''))} / last {html.escape(str(issue.get('last_seen') or ''))}</small>{payload_preview}</td>
  <td>{int(issue.get('report_count') or 0)}</td>
  <td>
    <form method="post" action="/admin/issue">
      <input type="hidden" name="token" value="{html.escape(token)}">
      <input type="hidden" name="issue_id" value="{issue_id}">
      <select name="status">{options}</select>
      <input name="fixed_version" placeholder="0.2.32" value="{fixed_version}">
      <input name="fix_note" placeholder="fix note" value="{fix_note}">
      <button>Save</button>
    </form>
  </td>
</tr>""")

        client_rows = []
        for client in clients:
            client_rows.append(f"""
<tr>
  <td><code>{html.escape(str(client.get('client_id') or ''))}</code></td>
  <td>{html.escape(str(client.get('app_version') or ''))}</td>
  <td>{html.escape(str(client.get('mode') or ''))}</td>
  <td>{html.escape(str(client.get('last_seen') or ''))}</td>
  <td><code>{html.escape(str(client.get('hardware_hash') or ''))}</code></td>
</tr>""")

        return f"""<!doctype html>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SDR Town Diagnostics Admin</title>
<style>
body {{ font-family: Segoe UI, Arial, sans-serif; margin: 24px; background: #101418; color: #e7edf3; }}
a {{ color: #8cc8ff; }}
table {{ border-collapse: collapse; width: 100%; margin: 16px 0 32px; }}
th, td {{ border-bottom: 1px solid #2b3540; padding: 8px; text-align: left; vertical-align: top; }}
input, select, button {{ margin: 2px; padding: 5px; background: #17212b; color: #e7edf3; border: 1px solid #536171; border-radius: 4px; }}
button {{ cursor: pointer; background: #245b91; }}
code {{ color: #9ee493; }}
small {{ color: #a9b7c6; }}
pre {{ white-space: pre-wrap; overflow-wrap: anywhere; max-height: 260px; overflow: auto; background: #0c1014; padding: 8px; border: 1px solid #2b3540; }}
.summary span {{ margin-right: 16px; }}
</style>
<h1>SDR Town Diagnostics Admin</h1>
<p class="summary">
  <span>Outstanding: <b>{summary.get('outstanding', 0)}</b></span>
  <span>Fixed: <b>{summary.get('fixed', 0)}</b></span>
  <span>Unrequired: <b>{summary.get('unrequired', 0)}</b></span>
  <span>Events: <b>{self.state.count}</b></span>
</p>
<p><a href="/api/issues?token={token_q}">issues JSON</a> | <a href="/api/clients?token={token_q}">clients JSON</a></p>
<h2>Issues</h2>
<table>
<tr><th>ID</th><th>Title</th><th>Reports</th><th>Status / Fix</th></tr>
{''.join(issue_rows) or '<tr><td colspan="4">No tracked issues yet.</td></tr>'}
</table>
<h2>Recent Clients</h2>
<table>
<tr><th>Client ID</th><th>Version</th><th>Mode</th><th>Last Seen</th><th>Hardware Hash</th></tr>
{''.join(client_rows) or '<tr><td colspan="5">No clients yet.</td></tr>'}
</table>"""


class DiagnosticsServer(ThreadingHTTPServer):
    def __init__(self, addr: tuple[str, int], state: DiagnosticsState) -> None:
        super().__init__(addr, Handler)
        self.state = state


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Collect compact SDR Town remote diagnostics JSONL events.")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host, use 0.0.0.0 for LAN/VPS testing")
    parser.add_argument("--port", type=int, default=8787, help="Bind port")
    parser.add_argument("--token", default=None, help="Optional bearer token required from clients")
    parser.add_argument("--admin-token", default=None, help="Optional separate token for /admin and /api endpoints")
    parser.add_argument("--out", default="remote_diagnostics", help="Output directory for session JSONL files")
    parser.add_argument("--max-bytes", type=int, default=65536, help="Maximum accepted request bytes")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = DiagnosticsState(Path(args.out), args.token, args.admin_token, max(2048, args.max_bytes))
    server = DiagnosticsServer((args.host, args.port), state)
    print(f"SDR Town diagnostics server listening on http://{args.host}:{args.port}/ingest")
    print(f"Admin UI: http://{args.host}:{args.port}/admin")
    print(f"Writing JSONL sessions to {state.out_dir.resolve()}")
    print(f"Issue database: {state.db_path.resolve()}")
    if args.token:
        print("Bearer token required for client ingest/status.")
    if args.admin_token:
        print("Separate admin token required for admin/API.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping diagnostics server.", file=sys.stderr)
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
