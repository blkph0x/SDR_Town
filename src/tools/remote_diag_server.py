#!/usr/bin/env python3
"""SDR Town remote diagnostics collector with lightweight issue tracking.

The collector accepts compact JSON events and separately consented, bounded
Inmarsat modem recordings. Raw events are kept as JSONL, while warning/error
events are grouped into issues in SQLite so fixes can be marked and reported
back to the affected installation on the next app start.
"""

from __future__ import annotations

import argparse
import base64
import uuid
from contextlib import contextmanager
import hashlib
import hmac
import html
import json
import math
import re
import sqlite3
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from http.cookies import SimpleCookie, CookieError
from pathlib import Path
from typing import Any, Iterator
from urllib.parse import parse_qs, quote, unquote, urlparse, urlencode


SESSION_RE = re.compile(r"[^A-Za-z0-9_.-]+")
LONG_HEX_RE = re.compile(r"\b[0-9a-fA-F]{8,}\b")
LONG_NUM_RE = re.compile(r"\b\d{5,}\b")
UUID_RE = re.compile(r"\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b")
ISSUE_STATUSES = {"outstanding", "fixed", "unrequired"}
ISSUE_SEVERITIES = {"warn", "warning", "error", "critical", "fatal"}
# DEC-0163: explicitly authorized tester allowance; size/rate/storage caps remain.
RECORDINGS_PER_DAY = 15


def valid_event(event: Any) -> bool:
    """DEC-0139: bounded technical envelopes, not arbitrary uploads."""
    if not isinstance(event, dict) or event.get("schema") != "sdr-town-remote-diagnostics-v1":
        return False
    if event.get("app") != "SDR_Town" or not isinstance(event.get("payload"), dict):
        return False
    for field in ("clientId", "sessionId"):
        value = event.get(field)
        if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,96}", value):
            return False
    if event.get("severity") not in {"debug", "info", "warn", "warning", "error", "critical", "fatal"}:
        return False
    if not isinstance(event.get("type"), str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,80}", event["type"]):
        return False
    nodes = 0

    def bounded(value: Any, depth: int) -> bool:
        nonlocal nodes
        nodes += 1
        if nodes > 4096 or depth > 12:
            return False
        if isinstance(value, dict):
            return len(value) <= 256 and all(
                isinstance(k, str) and len(k) <= 128 and bounded(v, depth + 1)
                for k, v in value.items())
        if isinstance(value, list):
            return len(value) <= 512 and all(bounded(v, depth + 1) for v in value)
        if isinstance(value, str):
            return len(value) <= 16384
        if isinstance(value, float):
            return math.isfinite(value)
        return value is None or isinstance(value, (bool, int))

    return bounded(event, 0)


def valid_recording(value: Any) -> bool:
    """DEC-0155: exact modem recording schema, never arbitrary file uploads."""
    if not isinstance(value, dict) or set(value) != {
        "schema", "channelHz", "mode", "timeUtc", "ifRate", "pcmRate", "format",
        "ifBase64", "pcmBase64", "clientId", "version", "before", "after",
        "iqBase64", "iqRate", "iqCenterHz", "iqStartSample", "iqFormat"
    }:
        return False
    if (value["schema"] != "sdr-town-inmarsat-recording-v1" or
            value["ifRate"] != 48000 or value["pcmRate"] != 8000 or value["format"] != "s16le"):
        return False
    if type(value["mode"]) is not int or value["mode"] not in (0, 1, 2, 3, 5, 6):
        return False
    hz = value["channelHz"]
    if type(hz) not in (int, float) or not math.isfinite(hz) or not 0 < hz <= 10e9:
        return False
    if value["iqFormat"] != "cf32_le" or not isinstance(value["iqStartSample"],str) or not re.fullmatch(r"[0-9]{1,20}",value["iqStartSample"]):
        return False
    for field, upper in (("iqRate",40e6),("iqCenterHz",10e9)):
        v=value[field]
        if type(v) not in (int,float) or not math.isfinite(v) or not 0 < v <= upper:
            return False
    for field in ("before", "after"):
        counters = value[field]
        if (not isinstance(counters, dict) or set(counters) - {
                "input48k", "crcOk", "crcBad", "pcmSamples", "codecErrors", "codecMutes",
                "acarsAirToGround", "acarsGroundToAir", "acarsUnknownDirection", "adscDecoded",
                "positionReports", "positionIdentityMismatches", "applicationDecoded", "applicationInvalid",
                "applicationUnsupported", "applicationControl", "voiceAesId", "identityChanges",
                "speechFrames", "unidentifiedSpeechFrames"} or
                any(type(v) not in (int, float) or not math.isfinite(v) or v < 0 or v > 2**53
                    for v in counters.values())):
            return False
        if "voiceAesId" in counters and (counters["voiceAesId"] > 0xffffff or
                                         counters["voiceAesId"] != int(counters["voiceAesId"])):
            return False
    for field, pattern in (("clientId", r"[A-Za-z0-9_-]{1,96}"),
                           ("version", r"[A-Za-z0-9_.-]{1,64}"),
                           ("timeUtc", r"[0-9TZ:.+-]{1,40}")):
        if not isinstance(value[field], str) or not re.fullmatch(pattern, value[field]):
            return False
    try:
        for field, maximum in (("ifBase64", 480000), ("pcmBase64", 80000), ("iqBase64",131072)):
            if not isinstance(value[field], str) or len(value[field]) > (maximum + 2)//3*4:
                return False
            raw = base64.b64decode(value[field], validate=True)
            if len(raw) % 2 or len(raw) > maximum or (field == "ifBase64" and len(raw) != maximum):
                return False
            if field=="iqBase64" and (not raw or len(raw)%8):
                return False
    except (ValueError, TypeError):
        return False
    return True


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
        self.rate_lock = threading.Lock()
        self.rate_window = time.monotonic()
        self.rate_clients: dict[str, tuple[int, int]] = {}
        self.rate_bytes = 0
        self.recording_window = time.monotonic()
        self.recording_requests = 0
        self.db_path = out_dir / "diagnostics.sqlite3"
        out_dir.mkdir(parents=True, exist_ok=True)
        self._init_db()
        self._backfill_events()

    def allow_event(self, client: str, size: int) -> bool:
        # Global ceiling prevents bypass by inventing unlimited client IDs.
        # Per-client quota permits bounded reconnect bursts above app's 64 KiB/min.
        with self.rate_lock:
            now = time.monotonic()
            if now - self.rate_window >= 60:
                self.rate_window = now
                self.rate_clients.clear()
                self.rate_bytes = 0
            count, total = self.rate_clients.get(client, (0, 0))
            if (count >= 120 or total + size > 128 * 1024 or
                    self.rate_bytes + size > 16 * 1024 * 1024 or
                    (client not in self.rate_clients and len(self.rate_clients) >= 2048)):
                return False
            self.rate_clients[client] = (count + 1, total + size)
            self.rate_bytes += size
            return True

    def allow_recording_request(self) -> bool:
        # Global request budget cannot be bypassed by fabricated installation IDs.
        with self.rate_lock:
            now=time.monotonic()
            if now-self.recording_window >= 60:
                self.recording_window=now;self.recording_requests=0
            if self.recording_requests >= 16:
                return False
            self.recording_requests += 1
            return True

    @contextmanager
    def _connect(self) -> Iterator[sqlite3.Connection]:
        con = sqlite3.connect(self.db_path)
        con.row_factory = sqlite3.Row
        try:
            # SQLite's context manager commits/rolls back, but does not close.
            # Explicit closure avoids retained handles across Python versions.
            with con:
                yield con
        finally:
            con.close()

    def _init_db(self) -> None:
        with self.lock, self._connect() as con:
            con.executescript(
                """
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS recordings (
                    id TEXT PRIMARY KEY, client TEXT NOT NULL, created REAL NOT NULL,
                    bytes INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT, fingerprint TEXT NOT NULL UNIQUE,
                    received REAL NOT NULL, client_id TEXT NOT NULL, session_id TEXT NOT NULL,
                    event_type TEXT NOT NULL, severity TEXT NOT NULL, version TEXT NOT NULL,
                    time_utc TEXT NOT NULL, envelope TEXT NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_events_client ON events(client_id,id);
                CREATE INDEX IF NOT EXISTS idx_events_session ON events(session_id,id);
                CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type,id);
                CREATE TABLE IF NOT EXISTS collector_metadata (key TEXT PRIMARY KEY,value TEXT);
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

    @staticmethod
    def _index_event(con: sqlite3.Connection, event: dict[str, Any], received: float) -> None:
        body = json.dumps(event, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
        con.execute("""INSERT OR IGNORE INTO events
            (fingerprint,received,client_id,session_id,event_type,severity,version,time_utc,envelope)
            VALUES (?,?,?,?,?,?,?,?,?)""", (
            hashlib.sha256(body.encode()).hexdigest(), received, event["clientId"],
            event["sessionId"], event["type"], event["severity"], str(event.get("version", ""))[:64],
            str(event.get("timeUtc", ""))[:64], body))

    def _backfill_events(self) -> None:
        # DEC-0160: bounded one-time index migration, never re-count issue reports.
        with self.lock, self._connect() as con:
            if con.execute("SELECT value FROM collector_metadata WHERE key='events_backfill_v1'").fetchone():
                return
            budget, count = 64*1024*1024, 0
            pending=[]
            files = sorted(self.out_dir.glob("*.jsonl"), key=lambda p:p.stat().st_mtime, reverse=True)
            for path in files:
                modified = path.stat().st_mtime
                if modified < time.time()-30*86400 or budget <= 0 or count >= 20000:
                    continue
                # Large legacy files: tail at most 4 MiB, skip the first partial line.
                with path.open("rb") as source:
                    start=max(0,path.stat().st_size-min(budget,4*1024*1024))
                    source.seek(start)
                    if start: source.readline(self.max_bytes+1)
                    data=source.read(min(budget,4*1024*1024))
                budget-=len(data)
                for line in reversed(data.splitlines()):
                    if count>=20000: break
                    if len(line)>self.max_bytes: continue
                    try: event=json.loads(line)
                    except (ValueError, UnicodeError): continue
                    if valid_event(event):
                        pending.append((event,modified));count+=1
            for event,modified in reversed(pending): self._index_event(con,event,modified)
            con.execute("INSERT INTO collector_metadata VALUES ('events_backfill_v1',?)",(str(count),))
        self.expire_events()

    def expire_events(self) -> None:
        with self.lock, self._connect() as con:
            con.execute("DELETE FROM events WHERE received<?",(time.time()-30*86400,))
            # Cap both row count and payload bytes. SQLite reuses freed pages.
            con.execute("""DELETE FROM events WHERE id IN (
                SELECT id FROM (SELECT id, row_number() OVER (ORDER BY id DESC) AS n,
                    sum(length(envelope)) OVER (ORDER BY id DESC) AS bytes FROM events)
                WHERE n>20000 OR bytes>67108864)""")
            files=sorted(self.out_dir.glob("*.jsonl"),key=lambda p:p.stat().st_mtime,reverse=True)
            total=0
            for path in files:
                info=path.stat();total+=info.st_size
                if info.st_mtime<time.time()-30*86400 or total>128*1024*1024:
                    path.unlink(missing_ok=True)

    def list_events(self, client: str = "", session: str = "", event_type: str = "",
                    severity: str = "", before: int = 0, limit: int = 50) -> dict[str, Any]:
        terms, args = [], []
        for column,value in (("client_id",client),("session_id",session),
                             ("event_type",event_type),("severity",severity)):
            if value: terms.append(column+"=?");args.append(value[:96])
        if before>0: terms.append("id<?");args.append(before)
        clause=" WHERE "+" AND ".join(terms) if terms else ""
        limit=max(1,min(100,limit))
        with self.lock, self._connect() as con:
            rows=con.execute("SELECT * FROM events"+clause+" ORDER BY id DESC LIMIT ?",[*args,limit+1]).fetchall()
            total=con.execute("SELECT COUNT(*) FROM events").fetchone()[0]
        items=[]
        for row in rows[:limit]:
            item=dict(row);item["event"]=json.loads(item.pop("envelope"));item.pop("fingerprint")
            items.append(item)
        return {"ok":True,"events":items,"indexedEvents":total,
                "nextBefore":items[-1]["id"] if len(rows)>limit else None}

    def save_recording(self, value: dict[str, Any]) -> str | None:
        if not valid_recording(value):
            raise ValueError("invalid recording")
        raw = json.dumps(value, separators=(",", ":")).encode("utf-8")
        directory = self.out_dir / "recordings"
        directory.mkdir(exist_ok=True)
        with self.lock, self._connect() as con:
            now = time.time()
            for row in con.execute("SELECT id FROM recordings WHERE created < ?", (now-30*86400,)):
                (directory / (row["id"] + ".json")).unlink(missing_ok=True)
            con.execute("DELETE FROM recordings WHERE created < ?", (now-30*86400,))
            count = con.execute("SELECT COUNT(*) FROM recordings WHERE client=? AND created>?",
                                (value["clientId"], now-86400)).fetchone()[0]
            # Filesystem total also counts orphan files after an interrupted DB commit.
            total = sum(p.stat().st_size for p in directory.glob("*.json"))
            if count >= RECORDINGS_PER_DAY or total + len(raw) > 128*1024*1024:
                return None
            identity = uuid.uuid4().hex
            path = directory / (identity + ".json")
            try:
                with path.open("xb") as output:
                    output.write(raw)
                con.execute("INSERT INTO recordings VALUES (?,?,?,?)",
                            (identity, value["clientId"], now, len(raw)))
            except Exception:
                path.unlink(missing_ok=True)
                raise
            return identity

    def expire_recordings(self) -> None:
        with self.lock, self._connect() as con:
            cutoff=time.time()-30*86400
            for row in con.execute("SELECT id FROM recordings WHERE created < ?", (cutoff,)):
                (self.out_dir / "recordings" / (row["id"] + ".json")).unlink(missing_ok=True)
            con.execute("DELETE FROM recordings WHERE created < ?", (cutoff,))

    def write_event(self, event: dict[str, Any]) -> Path:
        session = safe_name(event.get("sessionId"))
        path = self.out_dir / f"{session}.jsonl"
        with self.lock:
            if path.exists() and path.stat().st_size >= 4*1024*1024:
                path.replace(path.with_suffix(".previous.jsonl"))
            with path.open("a", encoding="utf-8") as f:
                f.write(json.dumps(event, ensure_ascii=True, separators=(",", ":")))
                f.write("\n")
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
            self._index_event(con,event,time.time())
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
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(body)

    def _query(self) -> dict[str, list[str]]:
        return parse_qs(urlparse(self.path).query)

    def _client_authorized(self) -> bool:
        if not self.state.token:
            return False
        return hmac.compare_digest(client_token_from_header(self.headers), self.state.token)

    def _admin_authorized(self, values: dict[str, list[str]] | None = None) -> bool:
        token = self.state.admin_token
        if not token:
            return False
        if hmac.compare_digest(client_token_from_header(self.headers), token):
            return True
        try:
            cookies=SimpleCookie(self.headers.get("Cookie", ""))
            cookie=cookies.get("diag_admin")
            parts=cookie.value.split(".") if cookie else []
            if len(parts)==3 and 0<=time.time()-int(parts[0])<8*3600:
                signed=".".join(parts[:2])
                expected=hmac.new(token.encode(),signed.encode(),hashlib.sha256).hexdigest()
                if hmac.compare_digest(expected,parts[2]): return True
        except (CookieError,ValueError):
            pass
        values = values if values is not None else self._query()
        supplied = values.get("token", [""])[0]
        return hmac.compare_digest(supplied, token)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = parse_qs(parsed.query)

        if path == "/health":
            self._send_json(200, {
                "ok": True,
                "recordingsPer24Hours": RECORDINGS_PER_DAY,
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

        if path == "/api/recordings":
            if not self._admin_authorized(query):
                self._send_json(401, {"ok": False, "error": "admin unauthorized"})
                return
            identity = query.get("id", [""])[0]
            if identity:
                if not re.fullmatch(r"[0-9a-f]{32}", identity):
                    self._send_json(400, {"ok": False, "error": "invalid id"})
                    return
                with self.state.lock, self.state._connect() as con:
                    row=con.execute("SELECT id FROM recordings WHERE id=? AND created>?",
                                    (identity, time.time()-30*86400)).fetchone()
                    file=self.state.out_dir / "recordings" / (identity+".json")
                    if row and file.is_file():
                        self._send_json(200, json.loads(file.read_bytes()))
                        return
                self._send_json(404, {"ok": False, "error": "not found"})
            else:
                with self.state.lock, self.state._connect() as con:
                    rows=con.execute("SELECT * FROM recordings WHERE created>? ORDER BY created DESC LIMIT 200",
                                     (time.time()-30*86400,)).fetchall()
                self._send_json(200, {"ok": True, "recordings": [dict(r) for r in rows]})
            return

        if path == "/api/clients":
            if not self._admin_authorized(query):
                self._send_json(401, {"ok": False, "error": "admin unauthorized"})
                return
            self._send_json(200, {"ok": True, "clients": self.state.list_clients()})
            return

        if path in {"/api/events", "/admin/events"}:
            if not self._admin_authorized(query):
                self._send_json(401, {"ok":False,"error":"admin unauthorized"})
                return
            try:
                result=self.state.list_events(
                    query.get("client",[""])[0],query.get("session",[""])[0],
                    query.get("type",[""])[0],query.get("severity",[""])[0],
                    int(query.get("before",["0"])[0]),int(query.get("limit",["25"])[0]))
            except ValueError:
                self._send_json(400,{"ok":False,"error":"invalid pagination"})
                return
            if path=="/api/events": self._send_json(200,result)
            else: self._send_html(200,self._events_page(query,result))
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

        if path.startswith("/admin/"):
            origin=self.headers.get("Origin")
            if origin and urlparse(origin).netloc!=self.headers.get("Host",""):
                self._send_json(403,{"ok":False,"error":"cross-origin admin request"});return

        if path=="/admin/login":
            try: length=int(self.headers.get("Content-Length","0"))
            except ValueError: length=0
            if not 0<length<=4096:
                self._send_json(400,{"ok":False,"error":"invalid login"});return
            form=parse_qs(self.rfile.read(length).decode("utf-8",errors="replace"))
            token=self.state.admin_token or ""
            if not token or not hmac.compare_digest(form.get("token",[""])[0],token):
                self._send_html(401,self._admin_login());return
            value=str(int(time.time()))+"."+uuid.uuid4().hex
            value+="."+hmac.new(token.encode(),value.encode(),hashlib.sha256).hexdigest()
            self.send_response(303)
            # Local HTTP admin is supported; the public proxy does not expose it.
            self.send_header("Set-Cookie","diag_admin="+value+"; HttpOnly; SameSite=Strict; Path=/; Max-Age=28800")
            self.send_header("Location","/admin")
            self.send_header("Cache-Control","no-store")
            self.end_headers();return

        if path == "/recordings":
            if not self._client_authorized():
                self._send_json(401, {"ok": False, "error": "unauthorized"})
                return
            if not self.state.allow_recording_request():
                self._send_json(429, {"ok": False, "error": "recording request quota"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                length = 0
            if not 0 < length <= 1024*1024:
                self._send_json(413, {"ok": False, "error": "recording size limit"})
                return
            if self.headers.get("Content-Type", "").split(";", 1)[0] != "application/json":
                self._send_json(415, {"ok": False, "error": "application/json required"})
                return
            self.connection.settimeout(10)
            try:
                raw = self.rfile.read(length)
                value = json.loads(raw)
                if len(raw) != length or not valid_recording(value):
                    raise ValueError("invalid recording")
            except (ValueError, TimeoutError, OSError):
                self._send_json(400, {"ok": False, "error": "invalid recording"})
                return
            try:
                identity = self.state.save_recording(value)
            except (OSError, sqlite3.Error):
                self._send_json(503, {"ok": False, "error": "recording storage unavailable"})
                return
            self._send_json(201 if identity else 429, {"ok": bool(identity), "recordingId": identity})
            return

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

        self.connection.settimeout(10)
        if self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower() != "application/json":
            self._send_json(415, {"ok": False, "error": "application/json required"})
            return
        try:
            raw = self.rfile.read(length)
        except TimeoutError:
            self._send_json(408, {"ok": False, "error": "request timeout"})
            return
        try:
            event = json.loads(raw.decode("utf-8"))
        except Exception as exc:
            self._send_json(400, {"ok": False, "error": f"bad json: {exc}"})
            return
        if not valid_event(event):
            self._send_json(400, {"ok": False, "error": "invalid diagnostics envelope"})
            return
        if not self.state.allow_event(event["clientId"], length):
            self._send_json(429, {"ok": False, "error": "diagnostics quota exceeded"})
            return

        try:
            path_written = self.state.write_event(event)
        except (OSError,sqlite3.Error):
            self._send_json(503,{"ok":False,"error":"diagnostics storage unavailable"})
            return
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
<form method="post" action="/admin/login">
  <label>Admin token <input name="token" type="password" autofocus></label>
  <button type="submit">Open</button>
</form>"""

    def _events_page(self, query: dict[str,list[str]], result: dict[str,Any]) -> str:
        esc=lambda value:html.escape(str(value),quote=True)
        fields="".join(f'<label>{label} <input name="{key}" value="{esc(query.get(key,[""])[0])}"></label>'
                       for key,label in (("client","Installation"),("session","Session")))
        with self.state.lock,self.state._connect() as con:
            types=[r[0] for r in con.execute("SELECT DISTINCT event_type FROM events ORDER BY event_type LIMIT 80")]
        for key,label,values in (("type","Event type",types),("severity","Severity",["debug","info","warn","warning","error","critical","fatal"])):
            selected=query.get(key,[""])[0]
            if selected and selected not in values: values.append(selected)
            options='<option value="">All</option>'+''.join(
                f'<option value="{esc(v)}" {"selected" if v==selected else ""}>{esc(v)}</option>' for v in values)
            fields+=f'<label>{label} <select name="{key}">{options}</select></label>'
        token=query.get("token",[""])[0]
        if token: fields+=f'<input type="hidden" name="token" value="{esc(token)}">'
        rows=[]
        for item in result["events"]:
            event=item["event"]
            link="/admin/events?"+urlencode({"client":item["client_id"],"session":item["session_id"],**({"token":token} if token else {})})
            payload=json.dumps(event.get("payload",{}),indent=2,ensure_ascii=False)
            rows.append(f'<tr><td>{esc(item["time_utc"])}<br>#{esc(event.get("seq",""))}</td>'
                f'<td>{esc(item["event_type"])}<br>{esc(item["severity"])}</td>'
                f'<td>{esc(item["version"])}<br><a href="{esc(link)}">{esc(item["client_id"])}</a>'
                f'<br><small>{esc(item["session_id"])}</small></td>'
                f'<td><details><summary>Report details</summary><pre>{esc(payload)}</pre></details></td></tr>')
        older=""
        if result["nextBefore"]:
            params={key:query[key][0] for key in ("client","session","type","severity","token") if key in query}
            params["before"]=str(result["nextBefore"])
            older=f'<a href="{esc("/admin/events?"+urlencode(params))}">Older reports</a>'
        return f'''<!doctype html><meta name="viewport" content="width=device-width, initial-scale=1">
<title>SDR Town report history</title><style>
body {{font:14px system-ui;margin:24px;background:#151819;color:#eee}}
a {{color:#7bc6eb}} form {{display:flex;flex-wrap:wrap;gap:12px}} label {{display:grid;gap:4px}}
input,select,button {{padding:7px;background:#252a2b;color:#fff;border:1px solid #777}}
table {{border-collapse:collapse;width:100%;margin:20px 0}} td,th {{padding:10px;text-align:left;border-bottom:1px solid #555;vertical-align:top}}
td {{overflow-wrap:anywhere}} pre {{white-space:pre-wrap;overflow-wrap:anywhere;max-height:420px;overflow:auto;max-width:850px}}
@media(max-width:700px) {{
table,tbody,tr,td {{display:block}} tr {{padding:12px 0;border-bottom:1px solid #555}}
tr:first-child {{display:none}} td {{padding:4px 0;border:0}} td:nth-child(2) {{font-weight:bold}}
pre {{max-width:100%;box-sizing:border-box}} input,select {{max-width:100%;box-sizing:border-box}}
}}
</style><h1>Report history</h1><p><a href="/admin">Issues and installations</a> |
{result["indexedEvents"]} indexed reports | Retention: 30 days, 20,000 reports, 64 MiB</p>
<form method="get" action="/admin/events">{fields}<button>Filter</button></form>
<table><tr><th>UTC / sequence</th><th>Event</th><th>Build / installation / session</th><th>Evidence</th></tr>
{''.join(rows) or '<tr><td colspan="4">No reports match these filters.</td></tr>'}</table>{older}'''

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
            history="/admin/events?"+urlencode({"client":str(client.get("client_id") or ""),"token":token})
            client_rows.append(f"""
<tr>
  <td><a href="{html.escape(history,quote=True)}"><code>{html.escape(str(client.get('client_id') or ''))}</code></a><br>{int(client.get('report_count') or 0)} reports</td>
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
<p><a href="/admin/events?token={token_q}">All reports / startup / settings / actions</a> |
<a href="/api/issues?token={token_q}">issues JSON</a> | <a href="/api/clients?token={token_q}">clients JSON</a></p>
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
        self.workers = threading.BoundedSemaphore(32)
        self.next_expiry = 0.0

    def service_actions(self):
        if time.monotonic() >= self.next_expiry:
            self.next_expiry=time.monotonic()+60
            try:
                self.state.expire_recordings()
                self.state.expire_events()
            except (OSError, sqlite3.Error):
                print("Recording retention maintenance failed", file=sys.stderr)

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(10)
        return connection, address

    def process_request(self, request, client_address):
        if not self.workers.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.workers.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.workers.release()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Collect compact SDR Town remote diagnostics JSONL events.")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host, use 0.0.0.0 for LAN/VPS testing")
    parser.add_argument("--port", type=int, default=8787, help="Bind port")
    parser.add_argument("--token", default=None, help="Optional bearer token required from clients")
    parser.add_argument("--admin-token", default=None, help="Optional separate token for /admin and /api endpoints")
    parser.add_argument("--token-file", type=Path, help="Read ingest credential without exposing it in the process arguments")
    parser.add_argument("--admin-token-file", type=Path, help="Read separate admin credential from a local file")
    parser.add_argument("--out", default="remote_diagnostics", help="Output directory for session JSONL files")
    parser.add_argument("--max-bytes", type=int, default=65536, help="Maximum accepted request bytes")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.token_file: args.token=args.token_file.read_text(encoding="utf-8-sig").strip()
    if args.admin_token_file: args.admin_token=args.admin_token_file.read_text(encoding="utf-8-sig").strip()
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
