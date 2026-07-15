#!/usr/bin/env python3
"""Summarize an SDR Town P25 IQ capture folder and suggest bounded replay tests."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


PATTERNS = {
    "group_instructions": "Instruction: Group",
    "auto_follow": "Auto-following P25 TG",
    "following_phase2": "Following Phase 2",
    "inband_follow": "P25 Phase 2 in-band follow",
    "voice_arm_refresh": "P25 voice arm refreshed",
    "scheduler": "P25 voice scheduler:",
    "scheduler_submitted": "P25 voice scheduler: submitted",
    "worker_start": "P25 DSP VOICE WORKER START",
    "worker_result": "P25 DSP VOICE WORKER:",
    "worker_stale": "P25 voice worker stale/drop",
    "audio_output": "P25 audio output:",
    "gate_emit": "gate=emit",
    "ess_clear": "ess=clear",
    "ess_unknown": "ess=unknown",
    "reset_pending": "resetPending=yes",
    "no_vcw": "block=no-vcw-from-live-window",
    "retune_traffic_source": "traffic source started",
}

TIME_RE = re.compile(r"\[(?P<local>[^\]|]+)\|\s*(?P<utc>[^]]+?) UTC\]")
TG_RE = re.compile(r"\btg=(?P<tg>\d+)\b", re.IGNORECASE)
CHANNEL_RE = re.compile(r"\bch=0X(?P<ch>[0-9A-Fa-f]+)\b", re.IGNORECASE)
CARRIER_RE = re.compile(r"\bcarrier=(?P<carrier>\d+)\b", re.IGNORECASE)
SLOT_RE = re.compile(r"\bslot=(?P<slot>[01])\b", re.IGNORECASE)
VOICE_RE = re.compile(r"\bvoice=(?P<voice>[0-9.]+)MHz\b", re.IGNORECASE)
NAC_FIELD_RE = re.compile(r"\bnac=0x(?P<nac>[0-9A-Fa-f]+)\b", re.IGNORECASE)
WACN_FIELD_RE = re.compile(r"\bwacn=0x(?P<wacn>[0-9A-Fa-f]+)\b", re.IGNORECASE)
SYSTEM_FIELD_RE = re.compile(r"\b(?:sys|system|systemid)=0x(?P<sys>[0-9A-Fa-f]+)\b", re.IGNORECASE)


def parse_mask_params_from_text(text: str) -> dict | None:
    nac_match = NAC_FIELD_RE.search(text)
    wacn_match = WACN_FIELD_RE.search(text)
    system_match = SYSTEM_FIELD_RE.search(text)
    if not (nac_match and wacn_match and system_match):
        return None
    return {
        "nac": int(nac_match.group("nac"), 16),
        "wacn": int(wacn_match.group("wacn"), 16),
        "system": int(system_match.group("sys"), 16),
    }


def parse_partial_mask_params_from_text(text: str) -> dict:
    out: dict = {}
    if nac_match := NAC_FIELD_RE.search(text):
        out["nac"] = int(nac_match.group("nac"), 16)
    if wacn_match := WACN_FIELD_RE.search(text):
        out["wacn"] = int(wacn_match.group("wacn"), 16)
    if system_match := SYSTEM_FIELD_RE.search(text):
        out["system"] = int(system_match.group("sys"), 16)
    return out


def extract_default_mask_params(lines: list[str]) -> dict | None:
    for line in lines:
        if "mask_params_known" not in line and "wacn=" not in line.lower():
            continue
        parsed = parse_mask_params_from_text(line)
        if parsed and parsed["nac"] > 0 and parsed["wacn"] > 0 and parsed["system"] > 0:
            return parsed
    return None


def parse_utc_from_line(line: str) -> datetime | None:
    match = TIME_RE.search(line)
    if not match:
        return None
    text = match.group("utc").replace("Z", "+00:00")
    try:
        return datetime.fromisoformat(text).astimezone(timezone.utc)
    except ValueError:
        return None


def default_capture_root() -> Path:
    user = Path(os.environ.get("USERPROFILE", str(Path.home())))
    return user / "AppData" / "Roaming" / "SDR_Town" / "SDR Town" / "iq_test_captures"


def latest_capture_dir(root: Path) -> Path:
    dirs = [p for p in root.iterdir() if p.is_dir()]
    if not dirs:
        raise FileNotFoundError(f"no capture folders under {root}")
    return max(dirs, key=lambda p: p.stat().st_mtime)


def find_capture_file(capture_dir: Path, suffix: str) -> Path | None:
    matches = sorted(capture_dir.glob(f"*{suffix}"))
    return matches[0] if matches else None


def load_summary(capture_dir: Path) -> dict:
    path = find_capture_file(capture_dir, "_summary.json")
    if not path:
        return {}
    return json.loads(path.read_text(encoding="utf-8", errors="replace"))


def parse_grants(lines: list[str], started_utc: datetime | None) -> list[dict]:
    grants: list[dict] = []
    for idx, line in enumerate(lines):
        if not (
            "Instruction: Group" in line
            or "selected_grant_event=" in line
            or "Group voice channel grant" in line
            or "Group voice channel grant update" in line
        ):
            continue
        tg_match = TG_RE.search(line)
        channel_match = CHANNEL_RE.search(line)
        voice_match = VOICE_RE.search(line)
        if not (tg_match and channel_match and voice_match):
            continue
        carrier_match = CARRIER_RE.search(line)
        slot_match = SLOT_RE.search(line)
        when = parse_utc_from_line(line)
        rel_ms = None
        if when and started_utc:
            rel_ms = max(0.0, (when - started_utc).total_seconds() * 1000.0)
        mask_params = parse_mask_params_from_text(line)
        grant = {
            "line": idx + 1,
            "utc": when.isoformat().replace("+00:00", "Z") if when else None,
            "relative_ms": rel_ms,
            "tg": int(tg_match.group("tg")),
            "channel_hex": channel_match.group("ch").upper(),
            "carrier": int(carrier_match.group("carrier")) if carrier_match else None,
            "slot": int(slot_match.group("slot")) if slot_match else None,
            "voice_mhz": float(voice_match.group("voice")),
            "phase2": "Phase 2" in line or "phase2-candidate" in line,
        }
        lower_line = line.lower()
        if re.search(r"\|\s*clear\s*(?:\||$)", lower_line) or " enc=clear" in lower_line:
            grant["arm_clear"] = "yes"
            grant["arm_encrypted"] = "no"
        elif re.search(r"\|\s*encrypted\s*(?:\||$)", lower_line) or " enc=encrypted" in lower_line or " enc=enc" in lower_line:
            grant["arm_clear"] = "no"
            grant["arm_encrypted"] = "yes"
        if mask_params:
            grant.update(mask_params)
        else:
            grant.update(parse_partial_mask_params_from_text(line))
        grants.append(grant)
    return grants


def cached_mask_params_for_grant(capture_dir: Path, grant: dict) -> dict | None:
    app_dir = capture_dir.parent.parent
    talkgroups_path = app_dir / "p25_talkgroups.json"
    if not talkgroups_path.is_file():
        return None
    try:
        rows = json.loads(talkgroups_path.read_text(encoding="utf-8", errors="replace"))
    except Exception:
        return None
    if not isinstance(rows, list):
        return None

    best: tuple[int, dict] | None = None
    grant_tg = int(grant.get("tg") or 0)
    grant_voice_hz = float(grant.get("voice_mhz") or 0.0) * 1e6
    for row in rows:
        if not isinstance(row, dict):
            continue
        if int(row.get("talkgroupId") or 0) != grant_tg:
            continue
        if not row.get("p25MaskParamsKnown"):
            continue
        nac = int(row.get("nac") or 0)
        wacn = int(row.get("wacn") or 0)
        system = int(row.get("systemId") or row.get("system") or 0)
        if nac <= 0 or wacn <= 0 or system <= 0:
            continue
        score = 10
        if int(grant.get("nac") or nac) == nac:
            score += 3
        if int(grant.get("system") or system) == system:
            score += 3
        row_voice_hz = float(row.get("lastVoiceFreqHz") or 0.0)
        if row_voice_hz > 0.0 and grant_voice_hz > 0.0:
            if abs(row_voice_hz - grant_voice_hz) <= 250.0:
                score += 10
            elif abs(row_voice_hz - grant_voice_hz) <= 25000.0:
                score += 2
        candidate = {"nac": nac, "wacn": wacn, "system": system}
        if best is None or score > best[0]:
            best = (score, candidate)
    return best[1] if best else None


def annotate_grants(grants: list[dict], lines: list[str]) -> None:
    for pos, grant in enumerate(grants):
        next_line = grants[pos + 1]["line"] if pos + 1 < len(grants) else len(lines) + 1
        window = lines[grant["line"] - 1 : max(grant["line"], min(len(lines), next_line - 1))]
        text = "\n".join(window)
        grant["inband_follow"] = "P25 Phase 2 in-band follow" in text
        grant["followed"] = "Auto-following P25 TG" in text or "Following Phase 2" in text
        grant["worker_start_seen"] = "P25 DSP VOICE WORKER START" in text
        grant["worker_result_seen"] = "P25 DSP VOICE WORKER:" in text
        grant["audio_output_seen"] = "P25 audio output:" in text
        refresh = re.search(r"P25 voice arm refreshed.*?clear=(?P<clear>\w+).*?encrypted=(?P<enc>\w+)", text)
        if refresh:
            grant["arm_clear"] = refresh.group("clear")
            grant["arm_encrypted"] = refresh.group("enc")


def grant_security_suffix(grant: dict) -> str:
    clear = str(grant.get("arm_clear", "")).strip().lower()
    encrypted = str(grant.get("arm_encrypted", "")).strip().lower()
    if encrypted in {"yes", "true", "1", "on"}:
        return " enc"
    if clear in {"yes", "true", "1", "on"}:
        return " clear"
    return ""


def grant_in_capture_passband(grant: dict, center_mhz: float, sample_rate_hz: float | int | None) -> bool:
    if center_mhz <= 0.0 or not sample_rate_hz:
        return True
    try:
        sr_hz = float(sample_rate_hz)
        voice_mhz = float(grant.get("voice_mhz") or 0.0)
    except (TypeError, ValueError):
        return True
    if sr_hz <= 0.0 or voice_mhz <= 0.0:
        return True
    # Match the live decoder's conservative usable passband guard.
    return abs((voice_mhz - center_mhz) * 1e6) <= sr_hz * 0.47


def recommended_commands(capture_dir: Path, summary: dict, grants: list[dict]) -> list[str]:
    center_mhz = float(summary.get("center_freq_hz") or summary.get("freq_hz") or 0.0) / 1e6
    sample_rate_hz = summary.get("sample_rate_hz")
    if center_mhz <= 0.0 and grants:
        center_mhz = grants[0]["voice_mhz"]
    out: list[str] = []
    suggested_voice = 0
    for grant in grants:
        if suggested_voice >= 6:
            break
        if not grant_in_capture_passband(grant, center_mhz, sample_rate_hz):
            grant["replay_skipped"] = "outside_capture_passband"
            continue
        skip_ms = max(0.0, float(grant.get("relative_ms") or 0.0) - 150.0)
        voice_mhz = float(grant["voice_mhz"])
        # One-RTL traffic retunes center the capture on the traffic channel, not CC.
        voice_center_arg = ""
        if abs(voice_mhz - center_mhz) > 0.10:
            voice_center_arg = f" voicecenter={voice_mhz:.5f}"
        if grant.get("phase2") and grant.get("slot") is not None:
            masks = ""
            if {"nac", "wacn", "system"}.issubset(grant):
                masks = f" nac=0x{grant['nac']:x} wacn=0x{grant['wacn']:x} system=0x{grant['system']:x}"
            security = grant_security_suffix(grant)
            out.append(
                f'p25 voicetest "{capture_dir}" {voice_mhz:.5f} '
                f"1800 skip={skip_ms:.0f} center={center_mhz:.5f}{voice_center_arg} "
                f'tg={grant["tg"]} slot={grant["slot"]} phase2{masks}{security}'
            )
            suggested_voice += 1
    if grants:
        first = grants[0]
        skip_ms = max(0.0, float(first.get("relative_ms") or 0.0) - 500.0)
        out.insert(0, f'p25 replay "{capture_dir}" {center_mhz:.5f} 900 skip={skip_ms:.0f} center={center_mhz:.5f}')
    return out


def audit_capture(capture_dir: Path) -> dict:
    summary = load_summary(capture_dir)
    if not summary:
        meta_path = find_capture_file(capture_dir, ".sigmf-meta")
        if meta_path:
            try:
                meta = json.loads(meta_path.read_text(encoding="utf-8", errors="replace"))
                global_meta = meta.get("global") if isinstance(meta, dict) else {}
                captures = meta.get("captures") if isinstance(meta, dict) else []
                first_capture = captures[0] if isinstance(captures, list) and captures else {}
                summary = {
                    "sample_rate_hz": global_meta.get("core:sample_rate"),
                    "center_freq_hz": first_capture.get("core:frequency"),
                    "started_utc": first_capture.get("core:datetime"),
                    "actual_seconds": global_meta.get("sdrtown:actual_seconds"),
                    "snr_db": global_meta.get("sdrtown:snr_db"),
                    "signal_level_db": global_meta.get("sdrtown:signal_level_db"),
                    "noise_floor_db": global_meta.get("sdrtown:noise_floor_db"),
                    "ring_overrun_samples": global_meta.get("sdrtown:ring_overrun_samples"),
                    "max_single_gap_samples": global_meta.get("sdrtown:max_single_gap_samples"),
                    "ring_epoch_resets": global_meta.get("sdrtown:ring_epoch_resets"),
                }
            except Exception:
                summary = {}
    log_path = find_capture_file(capture_dir, "_p25_log.txt")
    if not log_path:
        raise FileNotFoundError(f"no *_p25_log.txt in {capture_dir}")
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    counts = Counter()
    for line in lines:
        for key, needle in PATTERNS.items():
            if needle in line:
                counts[key] += 1
    started = summary.get("started_utc")
    started_utc = None
    if isinstance(started, str):
        started_utc = datetime.fromisoformat(started.replace("Z", "+00:00")).astimezone(timezone.utc)
    grants = parse_grants(lines, started_utc)
    default_mask_params = extract_default_mask_params(lines)
    if default_mask_params:
        for grant in grants:
            if grant.get("phase2") and not {"nac", "wacn", "system"}.issubset(grant):
                grant.update(default_mask_params)
    for grant in grants:
        if grant.get("phase2") and not {"nac", "wacn", "system"}.issubset(grant):
            cached_mask = cached_mask_params_for_grant(capture_dir, grant)
            if cached_mask:
                for key, value in cached_mask.items():
                    grant.setdefault(key, value)
    annotate_grants(grants, lines)
    health = {
        "health": summary.get("health"),
        "actual_seconds": summary.get("actual_seconds"),
        "sample_rate_hz": summary.get("sample_rate_hz"),
        "center_freq_hz": summary.get("center_freq_hz"),
        "snr_db": summary.get("snr_db"),
        "ring_overrun_samples": summary.get("ring_overrun_samples"),
        "max_single_gap_samples": summary.get("max_single_gap_samples"),
        "ring_epoch_resets": summary.get("ring_epoch_resets"),
    }
    findings: list[str] = []
    if health.get("ring_overrun_samples") == 0 and health.get("max_single_gap_samples") == 0:
        findings.append("capture_iq_gapless")
    if counts["group_instructions"] and counts["worker_result"] == 0:
        findings.append("grants_seen_but_no_worker_results")
    if counts["worker_start"] and counts["worker_result"] == 0:
        findings.append("worker_started_without_results")
    if counts["worker_stale"]:
        findings.append("worker_jobs_stale")
    if counts["audio_output"] == 0:
        findings.append("no_speaker_audio_pushed")
    return {
        "capture_dir": str(capture_dir),
        "log_path": str(log_path),
        "health": health,
        "counts": dict(counts),
        "grants": grants,
        "findings": findings,
        "recommended_cli_commands": recommended_commands(capture_dir, summary, grants),
    }


def run_self_test() -> None:
    lines = [
        "[00:00:01.000 | 2026-07-04T00:00:01.000Z UTC] Instruction: Group Update | tg=30003 | ch=0X7075 | carrier=58 | voice=420.72500MHz | P25 Phase 2 TDMA | slot=1 | nac=0X2DC | wacn=0XBEE00 | sys=0X2D1",
        "[00:00:01.001 | 2026-07-04T00:00:01.001Z UTC] P25 Phase 2 in-band follow: traffic is inside current RF passband",
        "[00:00:01.100 | 2026-07-04T00:00:01.100Z UTC] P25 voice scheduler: submitted iq=860160",
    ]
    started = datetime.fromisoformat("2026-07-04T00:00:00+00:00")
    grants = parse_grants(lines, started)
    annotate_grants(grants, lines)
    assert len(grants) == 1
    assert grants[0]["tg"] == 30003
    assert grants[0]["slot"] == 1
    assert grants[0]["inband_follow"] is True
    assert grants[0]["nac"] == 0x2DC
    print("p25_capture_audit self-test: PASS")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", help="Capture directory. Defaults to latest SDR Town capture.")
    parser.add_argument("--latest", action="store_true", help="Use the latest capture under the SDR Town capture root.")
    parser.add_argument("--root", type=Path, default=default_capture_root(), help="Capture root used with --latest.")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON.")
    parser.add_argument("--self-test", action="store_true", help="Run parser self-test and exit.")
    args = parser.parse_args(argv)
    if args.self_test:
        run_self_test()
        return 0
    capture_dir = latest_capture_dir(args.root) if args.latest or not args.capture else Path(args.capture)
    report = audit_capture(capture_dir)
    print(json.dumps(report, indent=2 if args.pretty else None, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
