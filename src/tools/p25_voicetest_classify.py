#!/usr/bin/env python3
"""Shared P25 voicetest/followtest result classification.

Rich classifier originally from run_p25_capture_replay_suite.py.
Keep PASS/FAIL labels identical across deep_audit and replay_suite.
"""

from __future__ import annotations

import re

PASS_AUDIO_STATUSES = frozenset(
    {
        "PASS_CONTINUOUS_AUDIO",
        "PASS_CLEAR_AUDIO",
        "PASS_PARTIAL_AUDIO",
    }
)


def _int_field(line: str, name: str) -> int:
    match = re.search(rf"\b{re.escape(name)}=(\d+)\b", line)
    return int(match.group(1)) if match else 0


def _ratio_field(line: str, name: str) -> tuple[int, int]:
    match = re.search(rf"\b{re.escape(name)}=(\d+)/(\d+)\b", line)
    return (int(match.group(1)), int(match.group(2))) if match else (0, 0)


def _refine_audio_status_from_final_line(final_status: str, final_line: str) -> str:
    decoded_frames = _int_field(final_line, "decodedFrames")
    speaker_samples = _int_field(final_line, "speakerSamples")
    target_vcw = _int_field(final_line, "targetVcw")
    emit_pcm = _int_field(final_line, "emitPcm")
    plc_frames = _int_field(final_line, "plc")
    iq_reject = _int_field(final_line, "iqReject")
    ambe_accepted, ambe_attempts = _ratio_field(final_line, "ambe")

    if target_vcw > 0 and ambe_attempts > 0 and ambe_accepted == 0 and iq_reject > 0:
        return "FAIL_PLC_ONLY_INPUT_QUALITY_REJECTED"
    if decoded_frames == 0 and speaker_samples > 0 and emit_pcm > 0 and plc_frames > 0:
        return "FAIL_CONCEALMENT_ONLY_AUDIO"
    if final_status in PASS_AUDIO_STATUSES and decoded_frames == 0:
        return "FAIL_CONCEALMENT_ONLY_AUDIO"
    return final_status


def classify_voicetest_output(output: str, timed_out: bool = False) -> str:
    """Classify CLI voicetest/followtest stdout into a stable status label."""
    if timed_out:
        return "TIMEOUT"
    result_matches = list(re.finditer(r"^P25 (?:voicetest|followtest) result=([A-Z0-9_]+).*$", output, flags=re.MULTILINE))
    if result_matches:
        final_status = result_matches[-1].group(1)
        final_line = result_matches[-1].group(0)
        final_status = _refine_audio_status_from_final_line(final_status, final_line)
        if final_status != "FAIL_NO_AUDIO":
            return final_status

        # Refinements below must use the final aggregate line only. Matching
        # earlier trace windows made failed replays look like PASS_UNKNOWN_GATED.
        final_line_match = re.findall(r"^P25 (?:voicetest|followtest) result=FAIL_NO_AUDIO.*$", output, flags=re.MULTILINE)
        final_line = final_line_match[-1] if final_line_match else final_line
        target_match = re.search(r"\btargetVcw=(\d+)\b", final_line)
        opp_match = re.search(r"\boppAmbe=(\d+)/(\d+)\b", final_line)
        probe_match = re.search(r"\bambeProbe=(\d+)/(\d+)\b", final_line)
        target_vcw = int(target_match.group(1)) if target_match else 0
        opp_attempts = int(opp_match.group(2)) if opp_match else 0
        probe_accepted = int(probe_match.group(1)) if probe_match else 0
        probe_attempts = int(probe_match.group(2)) if probe_match else 0
        if target_vcw > 0 and re.search(r"\bessKnown=no\b", final_line):
            return "FAIL_SECURITY_UNKNOWN_TARGET_VOICE_PROBED" if probe_attempts > 0 else "FAIL_SECURITY_UNKNOWN_TARGET_VOICE"
        if target_vcw == 0 and opp_attempts > 0:
            return "FAIL_OPPOSITE_SLOT_ONLY"
        if probe_accepted > 0:
            return "FAIL_AMBE_PROBE_ACCEPTED_BUT_GATED"
        if re.search(r"\bp2bursts=0\b", final_line):
            return "FAIL_NO_TRAFFIC_BURSTS"
        return "FAIL_NO_AUDIO"
    if "PASS_CONTINUOUS_AUDIO" in output:
        return _refine_audio_status_from_final_line("PASS_CONTINUOUS_AUDIO", output)
    if "PASS_CLEAR_AUDIO" in output:
        return _refine_audio_status_from_final_line("PASS_CLEAR_AUDIO", output)
    if "PASS_PARTIAL_AUDIO" in output:
        return _refine_audio_status_from_final_line("PASS_PARTIAL_AUDIO", output)
    if "PASS_ENCRYPTED_GATED" in output:
        return "PASS_ENCRYPTED_GATED"
    if "FAIL_PLC_ONLY_INPUT_QUALITY_REJECTED" in output:
        return "FAIL_PLC_ONLY_INPUT_QUALITY_REJECTED"
    if "FAIL_CONCEALMENT_ONLY_AUDIO" in output:
        return "FAIL_CONCEALMENT_ONLY_AUDIO"
    if "FAIL_RAW_AUDIO_GATED" in output:
        return "FAIL_RAW_AUDIO_GATED"
    if "FAIL_NO_TRAFFIC_BURSTS" in output:
        return "FAIL_NO_TRAFFIC_BURSTS"
    if "FAIL_NO_AUDIO" in output:
        if re.search(r"\bp2bursts=0\b", output) and not re.search(r"\bp2bursts=[1-9]\d*\b", output):
            return "FAIL_NO_TRAFFIC_BURSTS"
        target_values = [int(x) for x in re.findall(r"\btargetVcw=(\d+)\b", output)]
        opp_values = [int(x) for x in re.findall(r"\boppVcw=(\d+)\b", output)]
        ambe_probe = [tuple(map(int, m)) for m in re.findall(r"\bambeProbe=(\d+)/(\d+)\b", output)]
        target_vcw = max(target_values) if target_values else 0
        opp_vcw = max(opp_values) if opp_values else 0
        probe_accepted = max((a for a, _ in ambe_probe), default=0)
        probe_attempts = max((b for _, b in ambe_probe), default=0)
        if target_vcw > 0 and "p2ess=unknown" in output and re.search(r"\bp2mac=0/\d+\b", output):
            if (
                re.search(r"\baudioSamples=0\b", output)
                and re.search(r"\bspeakerSamples=0\b", output)
                and (
                    "gate=unknown-raw-queued-waiting-clear" in output
                    or "gate=unknown-waiting-clear" in output
                )
            ):
                return "PASS_UNKNOWN_GATED"
            return "FAIL_SECURITY_UNKNOWN_TARGET_VOICE_PROBED" if probe_attempts > 0 else "FAIL_SECURITY_UNKNOWN_TARGET_VOICE"
        if target_vcw == 0 and opp_vcw > 0:
            return "FAIL_OPPOSITE_SLOT_ONLY"
        if probe_accepted > 0:
            return "FAIL_AMBE_PROBE_ACCEPTED_BUT_GATED"
        return "FAIL_NO_AUDIO"
    if "NO_FOLLOW_CANDIDATE" in output:
        return "NO_FOLLOW_CANDIDATE"
    if "P25 replay load failed" in output or "P25 voicetest load failed" in output:
        return "LOAD_FAILED"
    if "P25 voicetest voice" in output or "P25 followtest control" in output:
        return "RAN_NO_PASS"
    return "NO_TEST_OUTPUT"


def is_pass_audio_status(status: str) -> bool:
    return status in PASS_AUDIO_STATUSES
