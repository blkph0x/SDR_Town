#!/usr/bin/env python3
"""Guard the live P25 diagnostic parser against command-line TG false positives."""

from __future__ import annotations

from run_p25_live_clear_audio_diag import parse_waitgrant_output, waitgrant_tg_event_lines


noise_only = """
P25 waitgrant monitoring 420.47500 MHz dev=0 seconds=120.0 follow=1 record=30.0 wav tg=10128
debug command: p25 waitgrant 420.47500 dev=0 seconds=120.0 follow record=30.0 wav tg=10128
P25 waitgrant t=5.0 sync trusted blocks=40 target TG=10128 still waiting
""".strip()

parsed_noise = parse_waitgrant_output(noise_only)
assert parsed_noise["grant_talkgroups_seen"] == [], parsed_noise["grant_talkgroups_seen"]
assert parsed_noise["grant_tg_sources_seen"] == [], parsed_noise["grant_tg_sources_seen"]
assert waitgrant_tg_event_lines(noise_only) == [], waitgrant_tg_event_lines(noise_only)

actual_events = """
P25 waitgrant monitoring 420.47500 MHz dev=0 seconds=120.0 follow=1 record=30.0 wav tg=10128
Instruction: Group Update | tg=30003 | ch=0X7075 | carrier=58 | voice=421.97500MHz | P25 Phase 2 TDMA | slot=1 | source=0x2365FB
P25 waitgrant grant selected: Following Phase 2: freq=421.97500MHz tg=30003 src=0x2365FB control=420.47500MHz enc=unknown slot=1 mask=known. TDMA sync: searching; MAC/ESS: pending.
P25 waitgrant following TG 30003 voice=421.975 MHz slot=1
P25 waitgrant saved decoded WAV audio: C:\\tmp\\tg30003.wav samples=48000 seconds=1.000
""".strip()

parsed_events = parse_waitgrant_output(actual_events)
assert parsed_events["grant_talkgroups_seen"] == [30003], parsed_events["grant_talkgroups_seen"]
assert parsed_events["wav_seconds"] == 1.0, parsed_events["wav_seconds"]
assert parsed_events["grant_tg_sources_seen"] == [
    {"tg": 30003, "source_id": 0x2365FB, "source_hex": "0x2365FB"}
], parsed_events["grant_tg_sources_seen"]

print("verify_p25_live_diag_waitgrant_parser: PASS")
