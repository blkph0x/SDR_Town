#!/usr/bin/env python3
"""Summarize an SDR Town P25 IQ capture folder and suggest bounded replay tests."""

from __future__ import annotations

import argparse
import csv
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
    "audio_top_up": "P25 audio top-up:",
    "auto_follow_skip_encrypted": "Auto-follow skipped encrypted P25 TG",
    "auto_follow_recent_encrypted_hold": "unknown grant update because an explicit encrypted grant",
    "auto_follow_sticky_encrypted_hold": "prior explicit encrypted state is still active",
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
SAME_RF_SLOT_HANDOFF_RE = re.compile(
    r"same-RF slot handoff: current TG (?P<current>\d+) on (?P<voice>[0-9.]+)MHz .*?"
    r"following new grant TG (?P<new>\d+) slot (?P<slot>[01]|unknown)",
    re.IGNORECASE,
)
GRANT_LINE_RE = re.compile(
    r"\bGrant:\s+TG=(?P<tg>\d+)\b.*?\bFREQ=(?P<freq>[0-9.]+)MHz\b.*?\bENC=(?P<enc>\w+)\b",
    re.IGNORECASE,
)
WORKER_TARGET_RE = re.compile(r"\btarget=(?P<target>[0-9.]+)MHz\b", re.IGNORECASE)
WORKER_CF_RE = re.compile(r"\bcf=(?P<cf>[0-9.]+)MHz\b", re.IGNORECASE)
WORKER_SR_RE = re.compile(r"\bsr=(?P<sr>[0-9.]+)MHz\b", re.IGNORECASE)
NAC_FIELD_RE = re.compile(r"\bnac=0x(?P<nac>[0-9A-Fa-f]+)\b", re.IGNORECASE)
WACN_FIELD_RE = re.compile(r"\bwacn=0x(?P<wacn>[0-9A-Fa-f]+)\b", re.IGNORECASE)
SYSTEM_FIELD_RE = re.compile(r"\b(?:sys|system|systemid)=0x(?P<sys>[0-9A-Fa-f]+)\b", re.IGNORECASE)
SOURCE_FIELD_RE = re.compile(r"\b(?:src|source|rid|radio|sourceId)=(?P<src>0x[0-9A-Fa-f]+|[0-9A-Fa-f]+)\b", re.IGNORECASE)
WORKER_INT_FIELD_RE = re.compile(
    r"\b(?P<key>decoded|speaker|targetVcw|oppVcw|gaps|fed|emitPcm|ctxVcw|ctxDrop|"
    r"reject|wrongSlot|dup|absDup|seqDrop|feedOrderIssues)=(?P<value>\d+)\b"
)
WORKER_DSP_RE = re.compile(r"\bdsp=(?P<value>\d+)us\b")
AUDIO_OUTPUT_FIELD_RE = re.compile(
    r"\b(?P<key>pushed|gate|decoded|targetVcw|oppVcw|fed|emitPcm|gaps|reject|wrongSlot|dup|absDup|seqDrop|"
    r"pendingQueued|pendingRel|p2mac|probe|ess|targetEss|targetSession|targetPtt|action|ringQueued|src|source|rid|call|grantEpoch|pttGen)=(?P<value>[^\s]+)"
)
AUDIO_TOP_UP_FIELD_RE = re.compile(
    r"\b(?P<key>TG|real|bridge|ringQueued|ringFill|underruns)=(?P<value>[^\s]+)",
    re.IGNORECASE,
)
SCHEDULER_SUBMITTED_RE = re.compile(
    r"P25 voice scheduler:\s+submitted\s+iq=(?P<iq>\d+)\s+fresh=(?P<fresh>\d+)\s+"
    r"context=(?P<context>\d+).*?\bsr=(?P<sr>[0-9.]+)MHz\b.*?\btg=(?P<tg>\d+)\b.*?"
    r"\bslot=(?P<slot>[01])\b",
    re.IGNORECASE,
)
AUDIO_PUSH_RE = re.compile(r"\bpushed=(?P<pushed>\d+)\s+samples\b", re.IGNORECASE)
AUDIO_UNDERRUN_RE = re.compile(r"\bunderruns=(?P<underruns>\d+)\b", re.IGNORECASE)
AUDIO_RING_FILL_RE = re.compile(r"\bringFill=(?P<fill>[0-9.]+)%", re.IGNORECASE)
AUDIO_RING_QUEUED_RE = re.compile(r"\bringQueued=(?P<queued>\d+)\b", re.IGNORECASE)
AUDIO_OUTPUT_RATE_HZ = 48000.0
CAPTURE_NAME_FREQ_RE = re.compile(r"(?P<freq>\d+(?:[._]\d+)?)MHz", re.IGNORECASE)


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


def parse_source_id_from_text(text: str) -> int | None:
    match = SOURCE_FIELD_RE.search(text)
    if not match:
        return None
    raw = match.group("src")
    try:
        if raw.lower().startswith("0x") or any(ch in "ABCDEFabcdef" for ch in raw):
            return int(raw, 16)
        return int(raw, 10)
    except ValueError:
        return None


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
    if not clear_context:
        return None

    reject = fields.get("reject", 0)
    opposite = fields.get("oppVcw", 0)
    wrong_slot = fields.get("wrongSlot", 0)
    non_target_reject = min(reject, max(opposite, wrong_slot))
    target_reject = max(0, reject - non_target_reject)
    accounted_target_drops = (
        target_reject
        + fields.get("dup", 0)
        + fields.get("absDup", 0)
        + fields.get("seqDrop", 0)
        + fields.get("ctxDrop", 0)
    )
    if accounted_target_drops >= target:
        duplicate_or_context_drops = (
            fields.get("dup", 0)
            + fields.get("absDup", 0)
            + fields.get("seqDrop", 0)
            + fields.get("ctxDrop", 0)
        )
        if target_reject <= 0 and duplicate_or_context_drops >= target:
            return "phase2-clear-target-vcw-duplicate-context-accounted"
        return "phase2-clear-target-vcw-rejected-before-feed"
    return "phase2-clear-target-vcw-not-fed"


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


def parse_loose_int(value: str | None) -> int | None:
    if value is None:
        return None
    text = str(value).strip().rstrip(".,;")
    if not text:
        return None
    try:
        return int(text, 0)
    except ValueError:
        return None


def parse_audio_top_up_fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for match in AUDIO_TOP_UP_FIELD_RE.finditer(line):
        fields[match.group("key").lower()] = match.group("value").rstrip(".")
    return fields


def scheduler_fresh_issue(line: str) -> tuple[str, dict] | None:
    match = SCHEDULER_SUBMITTED_RE.search(line)
    if not match:
        return None
    try:
        fresh = int(match.group("fresh"))
        context = int(match.group("context"))
        sample_rate_hz = float(match.group("sr")) * 1_000_000.0
        tg = int(match.group("tg"))
        slot = int(match.group("slot"))
    except ValueError:
        return None
    one_ambe_frame = int(sample_rate_hz * 0.020 + 0.5)
    two_ambe_frames = int(sample_rate_hz * 0.040 + 0.5)
    detail = {
        "fresh": fresh,
        "context": context,
        "sample_rate_hz": int(sample_rate_hz + 0.5),
        "one_ambe_frame_samples": one_ambe_frame,
        "two_ambe_frame_samples": two_ambe_frames,
        "tg": tg,
        "slot": slot,
    }
    if one_ambe_frame > 0 and fresh < one_ambe_frame:
        return "phase2-scheduler-subframe-fresh", detail
    if two_ambe_frames > 0 and fresh < two_ambe_frames:
        return "phase2-scheduler-less-than-two-frames-fresh", detail
    return None


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


def audio_output_session_issue(line: str) -> str | None:
    if "P25 audio output:" not in line:
        return None
    fields = parse_audio_output_fields(line)
    if not fields:
        return None
    pushed = parse_loose_int(fields.get("pushed")) or 0
    if pushed <= 0:
        return None
    tg_match = TG_RE.search(line)
    if not tg_match:
        return None
    tg = int(tg_match.group("tg"))
    if tg <= 0:
        return None
    call_session = parse_loose_int(fields.get("call"))
    if call_session is None:
        return None
    if call_session == 0:
        return "audio-output-missing-call-session"
    session_tg = call_session >> 32
    if session_tg != tg:
        return f"audio-output-call-session-talkgroup-mismatch:{session_tg}!={tg}"
    ptt_generation = parse_loose_int(fields.get("pttGen"))
    if ptt_generation == 0:
        return "audio-output-zero-ptt-generation"
    grant_epoch = parse_loose_int(fields.get("grantEpoch"))
    target_session = fields.get("targetSession", "no").lower().rstrip(".,;")
    if target_session == "yes" and grant_epoch == 0:
        return "audio-output-zero-grant-epoch"
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


def parse_audio_speaker_push_event(line: str) -> dict | None:
    source = ""
    pushed = 0
    bridge = 0
    if "P25 audio output:" in line:
        pushed_match = AUDIO_PUSH_RE.search(line)
        if not pushed_match:
            return None
        pushed = int(pushed_match.group("pushed"))
        source = "direct"
    elif "P25 audio top-up:" in line:
        fields = parse_audio_top_up_fields(line)
        try:
            pushed = int(fields.get("real", "0"))
            bridge = int(fields.get("bridge", "0"))
        except ValueError:
            return None
        source = "top_up"
    else:
        return None
    if pushed <= 0:
        return None

    when = parse_utc_from_line(line)
    underruns_match = AUDIO_UNDERRUN_RE.search(line)
    underruns = int(underruns_match.group("underruns")) if underruns_match else None
    ring_match = AUDIO_RING_FILL_RE.search(line)
    ring_fill = float(ring_match.group("fill")) if ring_match else None
    ring_queued_match = AUDIO_RING_QUEUED_RE.search(line)
    ring_queued = int(ring_queued_match.group("queued")) if ring_queued_match else None
    return {
        "utc": when,
        "pushed": pushed,
        "bridge": bridge,
        "source": source,
        "ring_queued_samples": ring_queued,
        "ring_fill_percent": ring_fill,
        "underruns": underruns,
    }


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
        event = parse_audio_speaker_push_event(line)
        if not event:
            continue
        pushed = event["pushed"]
        when = event["utc"]
        underruns = event["underruns"]
        ring_fill = event["ring_fill_percent"]
        ring_queued = event["ring_queued_samples"]

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
                    "ring_queued_samples": ring_queued,
                    "ring_fill_percent": ring_fill,
                    "underruns": underruns,
                    "source": event["source"],
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
            "source": event["source"],
            "bridge_samples": event["bridge"],
            "audio_ms": round((pushed / AUDIO_OUTPUT_RATE_HZ) * 1000.0, 3),
            "ring_queued_samples": ring_queued,
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
    latest_worker_source_by_tg: dict[int, dict] = {}
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
        if "P25 DSP VOICE WORKER" in line:
            tg_match = TG_RE.search(line)
            cf_match = WORKER_CF_RE.search(line)
            sr_match = WORKER_SR_RE.search(line)
            if tg_match and cf_match and sr_match:
                latest_worker_source_by_tg[int(tg_match.group("tg"))] = {
                    "line": line_number,
                    "center_mhz": float(cf_match.group("cf")),
                    "sample_rate_mhz": float(sr_match.group("sr")),
                    "source": "worker",
                }
        if hop := SAME_CALL_IN_SOURCE_HOP_RE.search(line):
            tg = int(hop.group("tg"))
            source = latest_worker_source_by_tg.get(tg) or active_source_by_tg.get(tg)
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


def same_rf_slot_handoff_before_current_clear_grant(lines: list[str]) -> list[dict]:
    """Find same-RF selected-slot steals immediately followed by a clear grant for the old slot."""
    bad: list[dict] = []
    pending: list[dict] = []
    for line_number, line in enumerate(lines, start=1):
        now_utc = parse_utc_from_line(line)
        if handoff := SAME_RF_SLOT_HANDOFF_RE.search(line):
            pending.append({
                "line": line_number,
                "utc": now_utc,
                "current_tg": int(handoff.group("current")),
                "new_tg": int(handoff.group("new")),
                "voice_mhz": float(handoff.group("voice")),
                "new_slot": handoff.group("slot"),
            })
            continue

        if not pending:
            continue
        if now_utc:
            pending = [
                item for item in pending
                if item["utc"] is None or (now_utc - item["utc"]).total_seconds() <= 1.5
            ]
        else:
            pending = [
                item for item in pending
                if line_number - item["line"] <= 40
            ]
        if not pending:
            continue
        grant = GRANT_LINE_RE.search(line)
        if not grant or grant.group("enc").lower() != "clear":
            continue
        grant_tg = int(grant.group("tg"))
        grant_freq_mhz = float(grant.group("freq"))
        for item in list(pending):
            if grant_tg != item["current_tg"]:
                continue
            if abs(grant_freq_mhz - item["voice_mhz"]) > 0.00005:
                continue
            grant_utc = parse_utc_from_line(line)
            delay_ms = None
            if item["utc"] and grant_utc:
                delay_ms = round((grant_utc - item["utc"]).total_seconds() * 1000.0, 3)
            bad.append({
                "line": item["line"],
                "utc": item["utc"].isoformat().replace("+00:00", "Z") if item["utc"] else None,
                "current_tg": item["current_tg"],
                "new_tg": item["new_tg"],
                "new_slot": item["new_slot"],
                "voice_mhz": item["voice_mhz"],
                "clear_grant_line": line_number,
                "clear_grant_delay_ms": delay_ms,
            })
            pending.remove(item)
            break
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


def capture_name_center_mhz(capture_dir: Path) -> float:
    matches = CAPTURE_NAME_FREQ_RE.findall(capture_dir.name)
    if not matches:
        return 0.0
    for raw in reversed(matches):
        try:
            mhz = float(raw.replace("_", "."))
        except ValueError:
            continue
        if 20.0 <= mhz <= 1000.0:
            return mhz
    return 0.0


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
        if (source_id := parse_source_id_from_text(line)) is not None:
            grant["source_id"] = source_id
        grants.append(grant)
    return grants


def grant_flag_enabled(grant: dict, key: str) -> bool:
    return str(grant.get(key, "")).strip().lower() in {"yes", "true", "1", "on"}


def grant_guarded_as_encrypted(grant: dict) -> bool:
    return (
        grant_flag_enabled(grant, "arm_encrypted")
        or bool(grant.get("skipped_encrypted"))
        or bool(grant.get("skipped_recent_encrypted_hold"))
        or bool(grant.get("skipped_sticky_encrypted_hold"))
        or bool(grant.get("skipped_sticky_encrypted_hold_inferred"))
    )


def annotate_inferred_encrypted_holds(grants: list[dict]) -> None:
    sticky_encrypted_tgs: set[int] = set()
    for grant in sorted(grants, key=lambda item: int(item.get("line") or 0)):
        try:
            tg = int(grant.get("tg") or 0)
        except (TypeError, ValueError):
            continue
        if tg <= 0:
            continue
        if grant_flag_enabled(grant, "arm_clear"):
            sticky_encrypted_tgs.discard(tg)
            continue
        if grant_flag_enabled(grant, "arm_encrypted"):
            sticky_encrypted_tgs.add(tg)
            continue
        if tg in sticky_encrypted_tgs and not grant_guarded_as_encrypted(grant):
            grant["skipped_sticky_encrypted_hold_inferred"] = True


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
        window_start = max(0, grant["line"] - 8)
        window = lines[window_start : max(grant["line"], min(len(lines), next_line - 1))]
        text = "\n".join(window)
        grant["inband_follow"] = "P25 Phase 2 in-band follow" in text
        grant["followed"] = "Auto-following P25 TG" in text or "Following Phase 2" in text
        grant["worker_start_seen"] = "P25 DSP VOICE WORKER START" in text
        grant["worker_result_seen"] = "P25 DSP VOICE WORKER:" in text
        grant["audio_output_seen"] = "P25 audio output:" in text
        grant["skipped_encrypted"] = "Auto-follow skipped encrypted P25 TG" in text
        grant["skipped_recent_encrypted_hold"] = (
            "unknown grant update because an explicit encrypted grant" in text
        )
        grant["skipped_sticky_encrypted_hold"] = (
            "prior explicit encrypted state is still active" in text
        )
        refresh = re.search(r"P25 voice arm refreshed.*?clear=(?P<clear>\w+).*?encrypted=(?P<enc>\w+)", text)
        if refresh:
            grant["arm_clear"] = refresh.group("clear")
            grant["arm_encrypted"] = refresh.group("enc")
        if "source_id" not in grant:
            if (source_id := parse_source_id_from_text(text)) is not None:
                grant["source_id"] = source_id
                grant["source_hex"] = f"0x{source_id:06X}"
        source = TRAFFIC_SOURCE_RE.search(text)
        if source:
            try:
                if int(source.group("tg")) == int(grant.get("tg") or 0):
                    grant["source_center_mhz"] = float(source.group("center"))
                    grant["source_sample_rate_mhz"] = float(source.group("sr"))
            except (TypeError, ValueError):
                pass
        elif grant.get("worker_start_seen"):
            cf_match = WORKER_CF_RE.search(text)
            sr_match = WORKER_SR_RE.search(text)
            if cf_match:
                try:
                    grant["source_center_mhz"] = float(cf_match.group("cf"))
                except ValueError:
                    pass
            if sr_match:
                try:
                    grant["source_sample_rate_mhz"] = float(sr_match.group("sr"))
                except ValueError:
                    pass


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


def grant_replay_center_mhz(grant: dict, capture_center_mhz: float) -> float:
    try:
        source_center_mhz = float(grant.get("source_center_mhz") or 0.0)
    except (TypeError, ValueError):
        source_center_mhz = 0.0
    if source_center_mhz > 0.0:
        return source_center_mhz
    return capture_center_mhz


def implausible_p25_voice_grants(grants: list[dict]) -> list[dict]:
    out: list[dict] = []
    for grant in grants:
        try:
            voice_mhz = float(grant.get("voice_mhz") or 0.0)
        except (TypeError, ValueError):
            voice_mhz = 0.0
        try:
            source_center_mhz = float(grant.get("source_center_mhz") or 0.0)
        except (TypeError, ValueError):
            source_center_mhz = 0.0
        if voice_mhz > 1000.0 or source_center_mhz > 1000.0:
            item = {
                "line": grant.get("line"),
                "utc": grant.get("utc"),
                "tg": grant.get("tg"),
                "channel_hex": grant.get("channel_hex"),
                "voice_mhz": voice_mhz,
            }
            if source_center_mhz > 0.0:
                item["source_center_mhz"] = source_center_mhz
            out.append(item)
    return out


def recommended_commands(capture_dir: Path, summary: dict, grants: list[dict]) -> list[str]:
    center_mhz = float(summary.get("center_freq_hz") or summary.get("freq_hz") or 0.0) / 1e6
    name_center_mhz = capture_name_center_mhz(capture_dir)
    if center_mhz > 1000.0 and name_center_mhz > 0.0:
        center_mhz = name_center_mhz
    sample_rate_hz = summary.get("sample_rate_hz")
    if center_mhz <= 0.0 and grants:
        center_mhz = grants[0]["voice_mhz"]
    out: list[str] = []
    suggested_voice = 0
    for grant in grants:
        if suggested_voice >= 6:
            break
        if implausible_p25_voice_grants([grant]):
            grant["replay_skipped"] = "implausible_p25_voice_frequency"
            continue
        replay_center_mhz = grant_replay_center_mhz(grant, center_mhz)
        if not grant_in_capture_passband(grant, replay_center_mhz, sample_rate_hz):
            grant["replay_skipped"] = "outside_capture_passband"
            continue
        skip_ms = max(0.0, float(grant.get("relative_ms") or 0.0) - 150.0)
        voice_mhz = float(grant["voice_mhz"])
        # One-RTL traffic retunes center the live stream on the traffic source,
        # while the capture metadata remains anchored to the original CC.  Replay
        # with the traffic source center when the log provides it; using the voice
        # frequency itself as the center hides the real offset and produces false
        # no-burst results.
        voice_center_arg = ""
        if replay_center_mhz > 0.0 and abs(replay_center_mhz - center_mhz) > 0.00001:
            voice_center_arg = f" voicecenter={replay_center_mhz:.5f}"
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


def summarize_audio_callback_rows(rows: list[dict]) -> dict:
    """Counter deltas, not inferred speech loss; totals aggregate all outputs."""
    fields = ["audio_consumed_frames", "audio_zero_fill_frames", "audio_empty_callbacks",
              "audio_partial_callbacks", "audio_control_silence_frames", "audio_producer_dropped_frames"]
    if len(rows) < 2 or any(field not in rows[0] for field in fields):
        return {"available": False, "reason": "capture lacks callback counters or two polls"}
    try:
        samples = [{field: int(row[field]) for field in fields} for row in rows]
    except (KeyError, TypeError, ValueError):
        return {"available": False, "reason": "invalid callback counter row"}
    if any(value < 0 for sample in samples for value in sample.values()):
        return {"available": False, "reason": "negative callback counter"}
    resets = sum(any(b[f] < a[f] for f in fields) for a, b in zip(samples, samples[1:]))
    if resets:
        return {"available": False, "reason": "callback counters reset during capture", "resets": resets}
    return {"available": True, "polls": len(rows),
            "deltas": {f: samples[-1][f] - samples[0][f] for f in fields},
            "interpretation": "Per-output totals. Empty callbacks include normal silence; partial callbacks are queue drain boundaries, not proof of missing speech."}


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
    audio_session_examples: list[dict] = []
    clear_target_feed_starvation_examples: list[dict] = []
    clear_target_rejected_before_feed_examples: list[dict] = []
    clear_target_accounted_without_feed_examples: list[dict] = []
    slow_voice_worker_examples: list[dict] = []
    scheduler_short_fresh_examples: list[dict] = []
    for line_number, line in enumerate(lines, start=1):
        for key, needle in PATTERNS.items():
            if needle in line:
                counts[key] += 1
        if "P25 DSP VOICE WORKER:" in line:
            fields = parse_worker_int_fields(line)
            if fields.get("absDup", 0) > 0:
                counts["worker_abs_duplicate_suppression"] += 1
            if fields.get("seqDrop", 0) > 0:
                counts["worker_sequencer_suppression"] += 1
        if issue := scheduler_fresh_issue(line):
            reason, detail = issue
            if reason == "phase2-scheduler-subframe-fresh":
                counts["scheduler_subframe_fresh_submit"] += 1
            else:
                counts["scheduler_short_fresh_submit"] += 1
            if len(scheduler_short_fresh_examples) < 8:
                utc = parse_utc_from_line(line)
                scheduler_short_fresh_examples.append({
                    "line": line_number,
                    "utc": utc.isoformat().replace("+00:00", "Z") if utc else None,
                    "reason": reason,
                    **detail,
                })
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
            if issue == "phase2-clear-target-vcw-rejected-before-feed":
                counts["clear_target_rejected_before_feed"] += 1
                examples = clear_target_rejected_before_feed_examples
            elif issue == "phase2-clear-target-vcw-duplicate-context-accounted":
                counts["clear_target_accounted_without_feed"] += 1
                examples = clear_target_accounted_without_feed_examples
            else:
                counts["clear_target_feed_starvation"] += 1
                examples = clear_target_feed_starvation_examples
            if len(examples) < 8:
                utc = parse_utc_from_line(line)
                examples.append({
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
        if issue := audio_output_session_issue(line):
            counts["audio_output_session_mismatch"] += 1
            counts[issue.split(":", 1)[0]] += 1
            if len(audio_session_examples) < 8:
                utc = parse_utc_from_line(line)
                audio_session_examples.append({
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
        if "P25 audio top-up:" in line:
            fields = parse_audio_top_up_fields(line)
            try:
                real = int(fields.get("real", "0"))
                bridge = int(fields.get("bridge", "0"))
            except ValueError:
                real = 0
                bridge = 0
            if real > 0:
                counts["audio_top_up_real"] += 1
                counts["audio_top_up_real_samples"] += real
            if bridge > 0:
                counts["audio_top_up_bridge"] += 1
                counts["audio_top_up_bridge_samples"] += bridge
    started = summary.get("started_utc")
    started_utc = None
    if isinstance(started, str):
        started_utc = datetime.fromisoformat(started.replace("Z", "+00:00")).astimezone(timezone.utc)
    grants = parse_grants(lines, started_utc)
    retune_stalls = traffic_retune_stalls(lines)
    stale_target_hops = same_call_stale_target_hops(lines)
    out_of_source_inplace_hops = same_call_out_of_source_inplace_hops(lines)
    same_rf_clear_steals = same_rf_slot_handoff_before_current_clear_grant(lines)
    audio_starvation = audio_output_starvation(lines)
    if retune_stalls:
        counts["traffic_retune_stall"] = len(retune_stalls)
    if stale_target_hops:
        counts["same_call_stale_target_hop"] = len(stale_target_hops)
    if out_of_source_inplace_hops:
        counts["same_call_out_of_source_inplace_hop"] = len(out_of_source_inplace_hops)
    if same_rf_clear_steals:
        counts["same_rf_slot_handoff_before_current_clear_grant"] = len(same_rf_clear_steals)
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
    annotate_inferred_encrypted_holds(grants)
    implausible_grants = implausible_p25_voice_grants(grants)
    encrypted_guarded_grants = sum(1 for grant in grants if grant_guarded_as_encrypted(grant))
    clear_grants = sum(1 for grant in grants if grant_flag_enabled(grant, "arm_clear"))
    unknown_unguarded_grants = max(0, len(grants) - encrypted_guarded_grants - clear_grants)
    if encrypted_guarded_grants:
        counts["encrypted_or_guarded_group_grants"] = encrypted_guarded_grants
    if clear_grants:
        counts["clear_group_grants"] = clear_grants
    if unknown_unguarded_grants:
        counts["unknown_unguarded_group_grants"] = unknown_unguarded_grants
    if implausible_grants:
        counts["implausible_p25_voice_frequency"] = len(implausible_grants)
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
        if grants and encrypted_guarded_grants == len(grants):
            findings.append("encrypted_grants_skipped_by_design")
        else:
            findings.append("grants_seen_but_no_worker_results")
    if counts["worker_start"] and counts["worker_result"] == 0:
        findings.append("worker_started_without_results")
    if counts["worker_stale"]:
        findings.append("worker_jobs_stale")
    speaker_audio_events = counts["audio_output"] + counts["audio_top_up_real"]
    if speaker_audio_events == 0:
        if grants and encrypted_guarded_grants == len(grants):
            findings.append("no_speaker_audio_expected_encrypted_only")
        else:
            findings.append("no_speaker_audio_pushed")
    if counts["mixed_slot_speaker_output"]:
        findings.append("mixed_slot_speaker_output")
    if counts["clear_target_feed_starvation"]:
        findings.append("clear_target_feed_starvation")
    if counts["clear_target_rejected_before_feed"]:
        findings.append("clear_target_rejected_before_feed")
    if counts["slow_voice_worker"]:
        findings.append("slow_voice_worker")
    if counts["unsafe_audio_output"]:
        findings.append("unsafe_audio_output")
    if counts["audio_output_session_mismatch"]:
        findings.append("audio_output_session_mismatch")
    if counts["audio_output_underpush"]:
        findings.append("audio_output_underpush")
    if counts["worker_abs_duplicate_suppression"]:
        findings.append("worker_abs_duplicate_suppression")
    if counts["worker_sequencer_suppression"]:
        findings.append("worker_sequencer_suppression")
    if counts["scheduler_subframe_fresh_submit"]:
        findings.append("phase2_scheduler_subframe_fresh")
    elif counts["scheduler_short_fresh_submit"]:
        findings.append("phase2_scheduler_short_fresh")
    if counts["traffic_retune_stall"]:
        findings.append("traffic_retune_stall")
    if counts["implausible_p25_voice_frequency"]:
        findings.append("implausible_p25_voice_frequency")
    if counts["same_call_stale_target_hop"]:
        findings.append("same_call_stale_target_hop")
    if counts["same_call_out_of_source_inplace_hop"]:
        findings.append("same_call_out_of_source_inplace_hop")
    if counts["same_rf_slot_handoff_before_current_clear_grant"]:
        findings.append("same_rf_slot_handoff_before_current_clear_grant")
    if counts["audio_output_sparse_gap"] and counts["audio_output_underrun_climb"]:
        findings.append("speaker_ring_starvation")
    callback_rows = []
    callback_path = next(capture_dir.glob("*_ring_health.csv"), None)
    if callback_path:
        with callback_path.open(encoding="utf-8-sig", newline="") as callback_file:
            callback_rows = list(csv.DictReader(callback_file))
    return {
        "capture_dir": str(capture_dir),
        "audio_callbacks": summarize_audio_callback_rows(callback_rows),
        "log_path": str(log_path),
        "health": health,
        "counts": dict(counts),
        "mixed_slot_speaker_examples": mixed_slot_speaker_examples,
        "clear_target_feed_starvation_examples": clear_target_feed_starvation_examples,
        "clear_target_rejected_before_feed_examples": clear_target_rejected_before_feed_examples,
        "clear_target_accounted_without_feed_examples": clear_target_accounted_without_feed_examples,
        "slow_voice_worker_examples": slow_voice_worker_examples,
        "scheduler_short_fresh_examples": scheduler_short_fresh_examples,
        "unsafe_audio_output_examples": unsafe_audio_output_examples,
        "audio_session_examples": audio_session_examples,
        "audio_underpush_examples": audio_underpush_examples,
        "audio_output_timing": audio_starvation,
        "traffic_retune_stalls": retune_stalls,
        "same_call_stale_target_hops": stale_target_hops,
        "same_call_out_of_source_inplace_hops": out_of_source_inplace_hops,
        "same_rf_slot_handoff_before_current_clear_grant": same_rf_clear_steals,
        "implausible_p25_voice_frequency_examples": implausible_grants[:8],
        "grants": grants,
        "findings": findings,
        "recommended_cli_commands": recommended_commands(capture_dir, summary, grants),
    }


def run_self_test() -> None:
    fields = ["audio_consumed_frames", "audio_zero_fill_frames", "audio_empty_callbacks",
              "audio_partial_callbacks", "audio_control_silence_frames", "audio_producer_dropped_frames"]
    first = {f: "10" for f in fields}
    last = {f: "20" for f in fields}
    assert summarize_audio_callback_rows([first, last])["deltas"][fields[0]] == 10
    assert not summarize_audio_callback_rows([last, first])["available"]
    assert not summarize_audio_callback_rows([{}, {}])["available"]
    assert not summarize_audio_callback_rows([first])["available"]
    assert not summarize_audio_callback_rows([first, {**last, fields[0]: "bad"}])["available"]
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
    retune_lines = [
        "[00:00:10.000 | 2026-07-04T00:00:10.000Z UTC] Instruction: Group Update | tg=30304 | ch=0X6090 | carrier=72 | voice=413.37500MHz | P25 Phase 2 TDMA | slot=0 | nac=0X2DF | wacn=0XBEE00 | sys=0X2D1",
        "[00:00:10.001 | 2026-07-04T00:00:10.001Z UTC] P25 traffic source started: TG=30304 voice=413.37500MHz slot=0 control=419.12500MHz dev=0 kind=single-rtl-retune-traffic-source-low-if sourceCenter=413.62500MHz sr=2.048MHz.",
        "[00:00:10.002 | 2026-07-04T00:00:10.002Z UTC] Auto-following P25 TG 30304 with independent traffic source voice=413.37500MHz control=419.12500MHz protocol=Phase 2 TDMA enc=unknown.",
    ]
    retune_grants = parse_grants(retune_lines, started)
    annotate_grants(retune_grants, retune_lines)
    assert len(retune_grants) == 1
    assert retune_grants[0]["source_center_mhz"] == 413.625
    assert grant_replay_center_mhz(retune_grants[0], 419.125) == 413.625
    encrypted_lines = [
        "[00:00:20.000 | 2026-07-04T00:00:20.000Z UTC] Instruction: Group Grant | tg=12068 | ch=0X6401 | carrier=512 | voice=418.87500MHz | SVC=0X44 | encrypted | P25 Phase 2 TDMA | slot=1 | nac=0X2DF | wacn=0XBEE00 | sys=0X2D1",
        "[00:00:20.001 | 2026-07-04T00:00:20.001Z UTC] Auto-follow skipped encrypted P25 TG 12068 (Phase 2 TDMA); staying on/returning to control channel.",
        "[00:00:22.000 | 2026-07-04T00:00:22.000Z UTC] Instruction: Group Update | tg=12068 | ch=0X6401 | carrier=512 | voice=418.87500MHz | P25 Phase 2 TDMA | slot=1 | nac=0X2DF | wacn=0XBEE00 | sys=0X2D1",
        "[00:00:22.001 | 2026-07-04T00:00:22.001Z UTC] Auto-follow skipped Phase 2 TG 12068 unknown grant update because an explicit encrypted grant for the same TG/channel/frequency was seen 2000ms earlier; matching sdrtrunk, wait for a fresh clear/current-call MAC or ESS before opening audio.",
        "[00:00:24.000 | 2026-07-04T00:00:24.000Z UTC] Instruction: Group Update | tg=12068 | ch=0X6401 | carrier=512 | voice=418.87500MHz | P25 Phase 2 TDMA | slot=1 | nac=0X2DF | wacn=0XBEE00 | sys=0X2D1",
        "[00:00:24.001 | 2026-07-04T00:00:24.001Z UTC] Auto-follow skipped Phase 2 TG 12068 OP=0x02 on 418.87500MHz because prior explicit encrypted state is still active; waiting for a fresh clear grant or traffic-channel MAC/ESS proof before audio.",
    ]
    encrypted_grants = parse_grants(encrypted_lines, started)
    annotate_grants(encrypted_grants, encrypted_lines)
    assert len(encrypted_grants) == 3
    assert all(grant_guarded_as_encrypted(grant) for grant in encrypted_grants)
    retune_commands = recommended_commands(
        Path("capture"),
        {"center_freq_hz": 419125000.0, "freq_hz": 419125000.0, "sample_rate_hz": 2048000.0},
        retune_grants,
    )
    assert any("voicecenter=413.62500" in cmd for cmd in retune_commands)
    assert all("voicecenter=413.37500" not in cmd for cmd in retune_commands)
    bad_plan_lines = [
        "[00:00:30.000 | 2026-07-04T00:00:30.000Z UTC] "
        "Instruction: Group Update | OP=0X02 | MFID=0X00 | Group voice channel grant update | "
        "id=7 | type=3 | tdma-slots=2 | base=2097.72160MHz | step=76.500kHz | "
        "tg=10609 | ch=0X7061 | carrier=48 | voice=2101.39360MHz | P25 Phase 2 TDMA | slot=1",
        "[00:00:30.001 | 2026-07-04T00:00:30.001Z UTC] "
        "P25 traffic source started: TG=10609 voice=2101.39360MHz slot=1 control=420.35000MHz "
        "dev=0 kind=single-rtl-retune-traffic-source-low-if sourceCenter=2101.38235MHz sr=2.048MHz.",
    ]
    bad_plan_grants = parse_grants(bad_plan_lines, started)
    annotate_grants(bad_plan_grants, bad_plan_lines)
    bad_plan = implausible_p25_voice_grants(bad_plan_grants)
    assert len(bad_plan) == 1
    assert bad_plan[0]["voice_mhz"] == 2101.39360
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
    same_rf_steal = same_rf_slot_handoff_before_current_clear_grant([
        "[00:00:10.000 | 2026-07-04T00:00:10.000Z UTC] "
        "P25 Phase 2 same-RF slot handoff: current TG 10301 on 419.87500MHz remained unacquired after the acquisition grace, "
        "so following new grant TG 30302 slot 1. This approximates sdrtrunk's independent traffic-slot handling on a single scanner receiver.",
        "[00:00:10.176 | 2026-07-04T00:00:10.176Z UTC] "
        "Grant: TG=10301 SRC=0X235B8A CH=0X64A0 ID=6 CHAN=0X4A0 CARRIER=592 SLOT=0 FREQ=419.87500MHz PHASE2=yes ENC=clear SVC=0X04 PRI=4 OP=0X00 MFID=0X00",
    ])
    assert len(same_rf_steal) == 1
    assert same_rf_steal[0]["current_tg"] == 10301
    assert same_rf_steal[0]["clear_grant_delay_ms"] == 176.0
    assert mixed_slot_speaker_issue(
        "[00:00:03.000 | 2026-07-04T00:00:03.000Z UTC] "
        "P25 DSP VOICE WORKER: gate=emit decoded=9 speaker=8640 targetVcw=9 oppVcw=4 gaps=0"
    ) is None
    assert clear_target_feed_starvation_issue(
        "[00:00:03.100 | 2026-07-04T00:00:03.100Z UTC] "
        "P25 DSP VOICE WORKER: diag=decoding clear voice gate=empty-audio decoded=0 "
        "speaker=0 targetVcw=3 oppVcw=4 fed=0 emitPcm=0 ess=clear dsp=15154us"
    ) == "phase2-clear-target-vcw-not-fed"
    assert clear_target_feed_starvation_issue(
        "[00:00:03.150 | 2026-07-04T00:00:03.150Z UTC] "
        "P25 DSP VOICE WORKER: diag=decoding clear voice gate=empty-audio decoded=0 "
        "speaker=0 targetVcw=8 oppVcw=4 fed=0 emitPcm=0 reject=12 wrongSlot=4 "
        "dup=0 absDup=0 seqDrop=0 ctxDrop=0 ess=clear dsp=43197us"
    ) == "phase2-clear-target-vcw-rejected-before-feed"
    assert clear_target_feed_starvation_issue(
        "[00:00:03.175 | 2026-07-04T00:00:03.175Z UTC] "
        "P25 DSP VOICE WORKER: diag=decoding clear voice gate=empty-audio decoded=0 "
        "speaker=0 targetVcw=10 oppVcw=4 fed=0 emitPcm=0 reject=0 wrongSlot=4 "
        "dup=0 absDup=10 seqDrop=0 ctxDrop=0 ess=clear dsp=43197us"
    ) == "phase2-clear-target-vcw-duplicate-context-accounted"
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
    sched_issue = scheduler_fresh_issue(
        "[00:00:04.035 | 2026-07-04T00:00:04.035Z UTC] "
        "P25 voice scheduler: submitted iq=196608 fresh=32768 context=163840 "
        "absKnown=yes sr=2.048MHz cf=420.97500MHz target=421.22500MHz tg=10330 slot=0 generation=1."
    )
    assert sched_issue is not None
    assert sched_issue[0] == "phase2-scheduler-subframe-fresh"
    assert sched_issue[1]["one_ambe_frame_samples"] == 40960
    assert audio_output_session_issue(
        "[00:00:04.040 | 2026-07-04T00:00:04.040Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=0 targetSession=yes call=0 grantEpoch=0 pttGen=0."
    ) == "audio-output-missing-call-session"
    assert audio_output_session_issue(
        "[00:00:04.042 | 2026-07-04T00:00:04.042Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=0 targetSession=yes call=45969034969089 grantEpoch=123 pttGen=1."
    ) == "audio-output-call-session-talkgroup-mismatch:10703!=30302"
    assert audio_output_session_issue(
        "[00:00:04.044 | 2026-07-04T00:00:04.044Z UTC] "
        "P25 audio output: TG=30302 pushed=3840 samples gate=emit decoded=4 "
        "targetVcw=4 oppVcw=0 targetSession=yes call=130146099003393 grantEpoch=123 pttGen=1."
    ) is None
    starvation = audio_output_starvation([
        "[00:00:05.000 | 2026-07-04T00:00:05.000Z UTC] "
        "P25 audio output: TG=30302 pushed=1920 samples gate=emit decoded=2 "
        "targetVcw=2 oppVcw=0 p2mac=1/9 ess=clear targetEss=clear "
        "targetSession=yes targetPtt=no action=target-session-clear activeOutputs=1 ringFill=6.0% underruns=10.",
        "[00:00:06.200 | 2026-07-04T00:00:06.200Z UTC] "
        "P25 audio top-up: TG=30302 real=3840 bridge=0 ringQueued=3840 ringFill=7.0% underruns=16.",
        "[00:00:06.240 | 2026-07-04T00:00:06.240Z UTC] "
        "P25 audio top-up: TG=30302 real=0 bridge=5760 ringQueued=9600 ringFill=18.0% underruns=16.",
    ])
    assert starvation["event_count"] == 2
    assert starvation["sparse_gap_count"] == 1
    assert starvation["underrun_climb_count"] == 1
    assert starvation["first_events"][1]["source"] == "top_up"
    assert parse_audio_speaker_push_event(
        "[00:00:07.000 | 2026-07-04T00:00:07.000Z UTC] "
        "P25 audio top-up: real=0 bridge=5760 ringQueued=9600 ringFill=18.0% underruns=16."
    ) is None
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
