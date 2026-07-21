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
RF_CENTER_RE = re.compile(r"\brfCenter=(?P<center>[0-9.]+)MHz\b", re.IGNORECASE)
SAME_CALL_HOP_RE = re.compile(
    r"same-call MHz hop pending: TG (?P<tg>\d+) voice (?P<old>[0-9.]+)MHz -> (?P<new>[0-9.]+)MHz",
    re.IGNORECASE,
)
TRAFFIC_SOURCE_RE = re.compile(
    r"traffic source started: TG=(?P<tg>\d+)\s+voice=(?P<voice>[0-9.]+)MHz.*?"
    r"sourceCenter=(?P<center>[0-9.]+)MHz\s+sr=(?P<sr>[0-9.]+)MHz",
    re.IGNORECASE,
)
SAME_CALL_IN_SOURCE_HOP_RE = re.compile(
    r"same-call in-(?:passband|source) channel hop: TG (?P<tg>\d+)\s+target\s+"
    r"(?P<old>[0-9.]+)MHz\s+->\s+(?P<new>[0-9.]+)MHz",
    re.IGNORECASE,
)
WORKER_TARGET_RE = re.compile(r"\btarget=(?P<target>[0-9.]+)MHz\b", re.IGNORECASE)
NAC_FIELD_RE = re.compile(r"\bnac=0x(?P<nac>[0-9A-Fa-f]+)\b", re.IGNORECASE)
WACN_FIELD_RE = re.compile(r"\bwacn=0x(?P<wacn>[0-9A-Fa-f]+)\b", re.IGNORECASE)
SYSTEM_FIELD_RE = re.compile(r"\b(?:sys|system|systemid)=0x(?P<sys>[0-9A-Fa-f]+)\b", re.IGNORECASE)
WORKER_INT_FIELD_RE = re.compile(r"\b(?P<key>decoded|speaker|targetVcw|oppVcw|gaps|fed|emitPcm)=(?P<value>\d+)\b")
WORKER_DSP_RE = re.compile(r"\bdsp=(?P<value>\d+)us\b")
AUDIO_OUTPUT_FIELD_RE = re.compile(
    r"\b(?P<key>pushed|gate|decoded|targetVcw|oppVcw|fed|emitPcm|gaps|reject|wrongSlot|dup|"
    r"pendingRel|p2mac|probe|ess|targetEss|targetSession|targetPtt|action)=(?P<value>[^\s]+)"
)
AUDIO_PUSH_RE = re.compile(r"\bpushed=(?P<pushed>\d+)\s+samples\b", re.IGNORECASE)
AUDIO_UNDERRUN_RE = re.compile(r"\bunderruns=(?P<underruns>\d+)\b", re.IGNORECASE)
AUDIO_RING_FILL_RE = re.compile(r"\bringFill=(?P<fill>[0-9.]+)%", re.IGNORECASE)
AUDIO_OUTPUT_RATE_HZ = 48000.0


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


def parse_worker_int_fields(line: str) -> dict[str, int]:
    return {match.group("key"): int(match.group("value")) for match in WORKER_INT_FIELD_RE.finditer(line)}


def mixed_slot_speaker_issue(line: str) -> str | None:
    if "P25 DSP VOICE WORKER:" not in line or "gate=emit" not in line:
        return None
    fields = parse_worker_int_fields(line)
    if fields.get("speaker", 0) <= 0 or fields.get("decoded", 0) <= 0:
        return None
    target = fields.get("targetVcw", 0)
    opposite = fields.get("oppVcw", 0)
    gaps = fields.get("gaps", 0)
    if opposite <= 0:
        return None
    # SDRTrunk keeps independent audio modules per TDMA timeslot. Once the
    # selected slot has already decoded and emitted PCM, companion-slot VCWs are
    # expected diagnostics, not an automatic mixed-audio fault.
    if target <= 0:
        return "phase2-opposite-slot-without-target"
    if gaps > 0 and fields.get("feedOrderIssues", 0) > 0:
        return "phase2-mixed-slot-order-issue"
    return None


def clear_target_feed_starvation_issue(line: str) -> str | None:
    if "P25 DSP VOICE WORKER:" not in line:
        return None
    fields = parse_worker_int_fields(line)
    target = fields.get("targetVcw", 0)
    fed = fields.get("fed", 0)
    if target <= 0 or fed > 0:
        return None
    clear_context = "ess=clear" in line or "diag=decoding clear voice" in line
    if clear_context:
        return "phase2-clear-target-vcw-not-fed"
    return None


def slow_voice_worker_issue(line: str) -> str | None:
    if "P25 DSP VOICE WORKER:" not in line:
        return None
    match = WORKER_DSP_RE.search(line)
    if not match:
        return None
    dsp_us = int(match.group("value"))
    if dsp_us < 200_000:
        return None
    fields = parse_worker_int_fields(line)
    if fields.get("targetVcw", 0) > 0 or "p2bursts=" in line:
        return "phase2-voice-worker-over-200ms"
    return None


def parse_audio_output_fields(line: str) -> dict[str, str]:
    return {match.group("key"): match.group("value") for match in AUDIO_OUTPUT_FIELD_RE.finditer(line)}


def audio_output_security_issue(line: str) -> str | None:
    if "P25 audio output:" not in line:
        return None
    fields = parse_audio_output_fields(line)
    if not fields:
        return None
    try:
        pushed = int(fields.get("pushed", "0"))
        target_vcw = int(fields.get("targetVcw", "0"))
        opposite_vcw = int(fields.get("oppVcw", "0"))
        rejected_vcw = int(fields.get("reject", "0"))
        wrong_slot_vcw = int(fields.get("wrongSlot", "0"))
    except ValueError:
        return None
    if pushed <= 0:
        return None
    if opposite_vcw > 0 and target_vcw <= 0:
        return "audio-output-with-opposite-slot-without-target"
    if (
        opposite_vcw > 0
        and target_vcw > 0
        and "wrongSlot" in fields
        and "reject" in fields
        and wrong_slot_vcw < opposite_vcw
        and rejected_vcw < opposite_vcw
    ):
        return "audio-output-opposite-slot-not-accounted"
    target_ess = fields.get("targetEss", "unknown").lower()
    target_ptt = fields.get("targetPtt", "no").lower()
    action = fields.get("action", "")
    if target_ess == "enc":
        return "audio-output-target-ess-encrypted"
    if (
        target_vcw > 0
        and target_ess == "unknown"
        and target_ptt != "yes"
    ):
        return f"audio-output-without-target-clear-proof:{action}"
    return None


def audio_output_underpush_issue(line: str) -> str | None:
    if "P25 audio output:" not in line:
        return None
    fields = parse_audio_output_fields(line)
    if not fields:
        return None
    try:
        pushed = int(fields.get("pushed", "0"))
        decoded = int(fields.get("decoded", "0"))
        emitted = int(fields.get("emitPcm", str(decoded)))
    except ValueError:
        return None
    pcm_frames = max(decoded, emitted)
    if pushed <= 0 or pcm_frames <= 0:
        return None
    expected_samples = pcm_frames * int(AUDIO_OUTPUT_RATE_HZ * 0.020)
    if expected_samples > 0 and pushed < expected_samples // 2:
        return "audio-output-underpushed-decoded-pcm"
    return None


def audio_output_starvation(lines: list[str]) -> dict:
    events: list[dict] = []
    sparse_gaps: list[dict] = []
    intervals_ms: list[float] = []
    underrun_climbs = 0
    last_dt: datetime | None = None
    last_line = 0
    last_pushed = 0
    last_underruns: int | None = None

    for line_number, line in enumerate(lines, start=1):
        if "P25 audio output:" not in line:
            continue
        pushed_match = AUDIO_PUSH_RE.search(line)
        if not pushed_match:
            continue
        pushed = int(pushed_match.group("pushed"))
        if pushed <= 0:
            continue
        when = parse_utc_from_line(line)
        underruns_match = AUDIO_UNDERRUN_RE.search(line)
        underruns = int(underruns_match.group("underruns")) if underruns_match else None
        ring_match = AUDIO_RING_FILL_RE.search(line)
        ring_fill = float(ring_match.group("fill")) if ring_match else None

        if last_dt and when:
            interval_ms = max(0.0, (when - last_dt).total_seconds() * 1000.0)
            intervals_ms.append(interval_ms)
            last_audio_ms = (last_pushed / AUDIO_OUTPUT_RATE_HZ) * 1000.0
            if interval_ms > 250.0 and interval_ms > max(250.0, last_audio_ms * 2.0):
                sparse_gaps.append({
                    "from_line": last_line,
                    "to_line": line_number,
                    "gap_ms": round(interval_ms, 3),
                    "previous_audio_ms": round(last_audio_ms, 3),
                    "pushed": pushed,
                    "ring_fill_percent": ring_fill,
                    "underruns": underruns,
                })
        if (
            last_underruns is not None
            and underruns is not None
            and underruns > last_underruns
        ):
            underrun_climbs += 1

        events.append({
            "line": line_number,
            "utc": when.isoformat().replace("+00:00", "Z") if when else None,
            "pushed": pushed,
            "audio_ms": round((pushed / AUDIO_OUTPUT_RATE_HZ) * 1000.0, 3),
            "ring_fill_percent": ring_fill,
            "underruns": underruns,
        })
        last_dt = when
        last_line = line_number
        last_pushed = pushed
        last_underruns = underruns

    sorted_intervals = sorted(intervals_ms)
    median_interval = None
    if sorted_intervals:
        median_interval = sorted_intervals[len(sorted_intervals) // 2]
    return {
        "event_count": len(events),
        "sparse_gap_count": len(sparse_gaps),
        "underrun_climb_count": underrun_climbs,
        "max_gap_ms": round(max(intervals_ms), 3) if intervals_ms else None,
        "median_interval_ms": round(median_interval, 3) if median_interval is not None else None,
        "first_events": events[:8],
        "sparse_gap_examples": sparse_gaps[:8],
    }


def traffic_retune_stalls(lines: list[str]) -> list[dict]:
    stalls: list[dict] = []
    active: dict | None = None
    for line_number, line in enumerate(lines, start=1):
        if "P25 one-RTL traffic source retuned primary tuner" in line:
            if active is not None:
                stalls.append(active)
            utc = parse_utc_from_line(line)
            voice_match = VOICE_RE.search(line)
            center_match = RF_CENTER_RE.search(line)
            active = {
                "line": line_number,
                "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                "voice_mhz": float(voice_match.group("voice")) if voice_match else None,
                "rf_center_mhz": float(center_match.group("center")) if center_match else None,
            }
            continue
        if active is None:
            continue
        if (
            "RF retuned back to control channel" in line
            or "follow returned to muted control channel" in line
            or "returning to control channel" in line
            or "returning to control." in line
        ):
            active = None
    if active is not None:
        stalls.append(active)
    return stalls


def same_call_stale_target_hops(lines: list[str]) -> list[dict]:
    stale: list[dict] = []
    active: dict | None = None
    for line_number, line in enumerate(lines, start=1):
        if match := SAME_CALL_HOP_RE.search(line):
            active = {
                "line": line_number,
                "tg": int(match.group("tg")),
                "old_mhz": float(match.group("old")),
                "new_mhz": float(match.group("new")),
            }
            continue
        if active is None:
            continue
        if ("P25 same-call channel hop:" in line or
                "P25 same-call in-passband channel hop:" in line or
                "P25 same-call in-source channel hop:" in line):
            active = None
            continue
        if "P25 DSP VOICE WORKER" not in line:
            continue
        tg_match = TG_RE.search(line)
        target_match = WORKER_TARGET_RE.search(line)
        if not (tg_match and target_match):
            continue
        if int(tg_match.group("tg")) != active["tg"]:
            continue
        target_mhz = float(target_match.group("target"))
        if abs(target_mhz - active["new_mhz"]) <= 0.00005:
            active = None
            continue
        if abs(target_mhz - active["old_mhz"]) <= 0.00005:
            stale.append({
                **active,
                "worker_line": line_number,
                "worker_target_mhz": target_mhz,
            })
            active = None
    return stale


def same_call_out_of_source_inplace_hops(lines: list[str]) -> list[dict]:
    bad: list[dict] = []
    active_source_by_tg: dict[int, dict] = {}
    for line_number, line in enumerate(lines, start=1):
        if source := TRAFFIC_SOURCE_RE.search(line):
            tg = int(source.group("tg"))
            active_source_by_tg[tg] = {
                "line": line_number,
                "voice_mhz": float(source.group("voice")),
                "center_mhz": float(source.group("center")),
                "sample_rate_mhz": float(source.group("sr")),
            }
            continue
        if hop := SAME_CALL_IN_SOURCE_HOP_RE.search(line):
            tg = int(hop.group("tg"))
            source = active_source_by_tg.get(tg)
            if not source:
                continue
            new_mhz = float(hop.group("new"))
            passband_limit_mhz = source["sample_rate_mhz"] * 0.42
            offset_mhz = abs(new_mhz - source["center_mhz"])
            if offset_mhz > passband_limit_mhz:
                utc = parse_utc_from_line(line)
                bad.append({
                    "line": line_number,
                    "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                    "tg": tg,
                    "old_mhz": float(hop.group("old")),
                    "new_mhz": new_mhz,
                    "source_center_mhz": source["center_mhz"],
                    "sample_rate_mhz": source["sample_rate_mhz"],
                    "offset_mhz": round(offset_mhz, 6),
                    "passband_limit_mhz": round(passband_limit_mhz, 6),
                    "source_line": source["line"],
                })
    return bad


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
        # One-RTL traffic retunes center the capture on the traffic channel, not
        # CC.  For in-passband wideband captures, keep center=<capture center>
        # and let the target voice frequency provide the offset; otherwise replay
        # misclassifies the TDMA slot.
        voice_center_arg = ""
        if not grant_in_capture_passband(grant, center_mhz, sample_rate_hz) and abs(voice_mhz - center_mhz) > 0.10:
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
    mixed_slot_speaker_examples: list[dict] = []
    unsafe_audio_output_examples: list[dict] = []
    audio_underpush_examples: list[dict] = []
    clear_target_feed_starvation_examples: list[dict] = []
    slow_voice_worker_examples: list[dict] = []
    for line_number, line in enumerate(lines, start=1):
        for key, needle in PATTERNS.items():
            if needle in line:
                counts[key] += 1
        if issue := mixed_slot_speaker_issue(line):
            counts["mixed_slot_speaker_output"] += 1
            counts[f"mixed_slot_speaker_{issue}"] += 1
            if len(mixed_slot_speaker_examples) < 8:
                utc = parse_utc_from_line(line)
                mixed_slot_speaker_examples.append({
                    "line": line_number,
                    "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                    "reason": issue,
                    **parse_worker_int_fields(line),
                })
        if issue := clear_target_feed_starvation_issue(line):
            counts["clear_target_feed_starvation"] += 1
            if len(clear_target_feed_starvation_examples) < 8:
                utc = parse_utc_from_line(line)
                clear_target_feed_starvation_examples.append({
                    "line": line_number,
                    "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                    "reason": issue,
                    **parse_worker_int_fields(line),
                })
        if issue := slow_voice_worker_issue(line):
            counts["slow_voice_worker"] += 1
            if len(slow_voice_worker_examples) < 8:
                utc = parse_utc_from_line(line)
                dsp = WORKER_DSP_RE.search(line)
                slow_voice_worker_examples.append({
                    "line": line_number,
                    "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                    "reason": issue,
                    "dsp_us": int(dsp.group("value")) if dsp else 0,
                    **parse_worker_int_fields(line),
                })
        if issue := audio_output_security_issue(line):
            counts["unsafe_audio_output"] += 1
            counts[issue.split(":", 1)[0]] += 1
            if len(unsafe_audio_output_examples) < 8:
                utc = parse_utc_from_line(line)
                unsafe_audio_output_examples.append({
                    "line": line_number,
                    "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                    "reason": issue,
                    **parse_audio_output_fields(line),
                })
        if issue := audio_output_underpush_issue(line):
            counts["audio_output_underpush"] += 1
            if len(audio_underpush_examples) < 8:
                utc = parse_utc_from_line(line)
                audio_underpush_examples.append({
                    "line": line_number,
                    "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                    "reason": issue,
                    **parse_audio_output_fields(line),
                })
    started = summary.get("started_utc")
    started_utc = None
    if isinstance(started, str):
        started_utc = datetime.fromisoformat(started.replace("Z", "+00:00")).astimezone(timezone.utc)
    grants = parse_grants(lines, started_utc)
    retune_stalls = traffic_retune_stalls(lines)
    stale_target_hops = same_call_stale_target_hops(lines)
    out_of_source_inplace_hops = same_call_out_of_source_inplace_hops(lines)
    audio_starvation = audio_output_starvation(lines)
    if retune_stalls:
        counts["traffic_retune_stall"] = len(retune_stalls)
    if stale_target_hops:
        counts["same_call_stale_target_hop"] = len(stale_target_hops)
    if out_of_source_inplace_hops:
        counts["same_call_out_of_source_inplace_hop"] = len(out_of_source_inplace_hops)
    if audio_starvation["sparse_gap_count"]:
        counts["audio_output_sparse_gap"] = audio_starvation["sparse_gap_count"]
    if audio_starvation["underrun_climb_count"]:
        counts["audio_output_underrun_climb"] = audio_starvation["underrun_climb_count"]
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
    if counts["mixed_slot_speaker_output"]:
        findings.append("mixed_slot_speaker_output")
    if counts["clear_target_feed_starvation"]:
        findings.append("clear_target_feed_starvation")
    if counts["slow_voice_worker"]:
        findings.append("slow_voice_worker")
    if counts["unsafe_audio_output"]:
        findings.append("unsafe_audio_output")
    if counts["audio_output_underpush"]:
        findings.append("audio_output_underpush")
    if counts["traffic_retune_stall"]:
        findings.append("traffic_retune_stall")
    if counts["same_call_stale_target_hop"]:
        findings.append("same_call_stale_target_hop")
    if counts["same_call_out_of_source_inplace_hop"]:
        findings.append("same_call_out_of_source_inplace_hop")
    if counts["audio_output_sparse_gap"] and counts["audio_output_underrun_climb"]:
        findings.append("speaker_ring_starvation")
    return {
        "capture_dir": str(capture_dir),
        "log_path": str(log_path),
        "health": health,
        "counts": dict(counts),
        "mixed_slot_speaker_examples": mixed_slot_speaker_examples,
        "clear_target_feed_starvation_examples": clear_target_feed_starvation_examples,
        "slow_voice_worker_examples": slow_voice_worker_examples,
        "unsafe_audio_output_examples": unsafe_audio_output_examples,
        "audio_underpush_examples": audio_underpush_examples,
        "audio_output_timing": audio_starvation,
        "traffic_retune_stalls": retune_stalls,
        "same_call_stale_target_hops": stale_target_hops,
        "same_call_out_of_source_inplace_hops": out_of_source_inplace_hops,
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
    stale = same_call_stale_target_hops([
        "[00:00:02.000 | 2026-07-04T00:00:02.000Z UTC] P25 auto-follow same-call MHz hop pending: TG 30302 voice 419.87500MHz -> 418.87500MHz; retuning traffic source before continuing metadata-only follow.",
        "[00:00:02.100 | 2026-07-04T00:00:02.100Z UTC] P25 DSP VOICE WORKER START: sr=2.048MHz cf=419.12500MHz target=419.87500MHz tg=30302 slot=0 generation=2.",
    ])
    assert len(stale) == 1
    out_of_source = same_call_out_of_source_inplace_hops([
        "[00:00:01.000 | 2026-07-04T00:00:01.000Z UTC] "
        "P25 traffic source started: TG=30302 voice=418.87500MHz slot=0 control=419.12500MHz "
        "dev=0 kind=same-wideband-control-source-low-if sourceCenter=419.12500MHz sr=2.048MHz.",
        "[00:00:02.000 | 2026-07-04T00:00:02.000Z UTC] "
        "P25 same-call in-passband channel hop: TG 30302 target 418.87500MHz -> 413.37500MHz; "
        "selected voice carrier, decoder state, audio de-dupe, and rolling IQ session were reset while RF/source center stayed unchanged.",
    ])
    assert len(out_of_source) == 1
    assert out_of_source[0]["tg"] == 30302
    assert mixed_slot_speaker_issue(
        "[00:00:03.000 | 2026-07-04T00:00:03.000Z UTC] "
        "P25 DSP VOICE WORKER: gate=emit decoded=9 speaker=8640 targetVcw=9 oppVcw=4 gaps=0"
    ) is None
    assert clear_target_feed_starvation_issue(
        "[00:00:03.100 | 2026-07-04T00:00:03.100Z UTC] "
        "P25 DSP VOICE WORKER: diag=decoding clear voice gate=empty-audio decoded=0 "
        "speaker=0 targetVcw=3 oppVcw=4 fed=0 emitPcm=0 ess=clear dsp=15154us"
    ) == "phase2-clear-target-vcw-not-fed"
    assert slow_voice_worker_issue(
        "[00:00:03.200 | 2026-07-04T00:00:03.200Z UTC] "
        "P25 DSP VOICE WORKER: gate=emit decoded=4 speaker=3840 "
        "targetVcw=4 oppVcw=0 fed=4 emitPcm=4 dsp=404756us"
    ) == "phase2-voice-worker-over-200ms"
    assert audio_output_security_issue(
        "[00:00:04.000 | 2026-07-04T00:00:04.000Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=0 p2mac=0/9 ess=unknown targetEss=unknown "
        "targetSession=no action=explicit-clear-grant-validated-release activeOutputs=1 ringFill=12.3% underruns=0."
    ) == "audio-output-without-target-clear-proof:explicit-clear-grant-validated-release"
    assert audio_output_security_issue(
        "[00:00:04.010 | 2026-07-04T00:00:04.010Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=2 p2mac=0/9 probe=2/2 ess=unknown targetEss=unknown "
        "targetSession=no action=explicit-clear-grant-validated-release activeOutputs=1 ringFill=12.3% underruns=0."
    ) == "audio-output-without-target-clear-proof:explicit-clear-grant-validated-release"
    assert audio_output_security_issue(
        "[00:00:04.015 | 2026-07-04T00:00:04.015Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=2 p2mac=0/9 probe=2/2 ess=unknown targetEss=unknown "
        "targetSession=no action=explicit-clear-grant-traffic-clear-release activeOutputs=1 ringFill=12.3% underruns=0."
    ) == "audio-output-without-target-clear-proof:explicit-clear-grant-traffic-clear-release"
    assert audio_output_security_issue(
        "[00:00:04.017 | 2026-07-04T00:00:04.017Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=2 p2mac=1/9 probe=0/2 ess=clear targetEss=clear "
        "targetSession=yes targetPtt=no action=explicit-clear-grant-traffic-clear-release activeOutputs=1 ringFill=12.3% underruns=0."
    ) is None
    assert audio_output_security_issue(
        "[00:00:04.020 | 2026-07-04T00:00:04.020Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=0 p2mac=0/9 probe=0/0 ess=unknown targetEss=unknown "
        "targetSession=yes targetPtt=no action=trusted-clear-pending-release activeOutputs=1 ringFill=12.3% underruns=0."
    ) == "audio-output-without-target-clear-proof:trusted-clear-pending-release"
    assert audio_output_security_issue(
        "[00:00:04.022 | 2026-07-04T00:00:04.022Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=0 p2mac=1/9 probe=0/0 ess=unknown targetEss=unknown "
        "targetSession=yes targetPtt=yes action=target-session-clear activeOutputs=1 ringFill=12.3% underruns=0."
    ) is None
    assert audio_output_security_issue(
        "[00:00:04.025 | 2026-07-04T00:00:04.025Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=2 fed=4 emitPcm=4 reject=0 wrongSlot=0 p2mac=1/9 "
        "probe=0/0 ess=clear targetEss=clear targetSession=yes targetPtt=no "
        "action=target-session-clear activeOutputs=1 ringFill=12.3% underruns=0."
    ) == "audio-output-opposite-slot-not-accounted"
    assert audio_output_underpush_issue(
        "[00:00:04.030 | 2026-07-04T00:00:04.030Z UTC] "
        "P25 audio output: TG=30302 pushed=480 samples gate=emit decoded=12 "
        "targetVcw=12 oppVcw=0 fed=12 emitPcm=12 p2mac=1/9 "
        "ess=clear targetEss=clear targetSession=yes targetPtt=no action=target-session-clear "
        "activeOutputs=1 ringFill=36.9% underruns=0."
    ) == "audio-output-underpushed-decoded-pcm"
    starvation = audio_output_starvation([
        "[00:00:05.000 | 2026-07-04T00:00:05.000Z UTC] "
        "P25 audio output: TG=30302 pushed=1920 samples gate=emit decoded=2 "
        "targetVcw=2 oppVcw=0 p2mac=1/9 ess=clear targetEss=clear "
        "targetSession=yes targetPtt=no action=target-session-clear activeOutputs=1 ringFill=6.0% underruns=10.",
        "[00:00:06.200 | 2026-07-04T00:00:06.200Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=0 p2mac=1/9 ess=clear targetEss=clear "
        "targetSession=yes targetPtt=no action=target-session-clear activeOutputs=1 ringFill=7.0% underruns=16.",
    ])
    assert starvation["event_count"] == 2
    assert starvation["sparse_gap_count"] == 1
    assert starvation["underrun_climb_count"] == 1
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
