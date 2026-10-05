"""DEC-0194: correlate passive capture events, without treating PCM as speech proof."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    return {"count": len(values), "p50_ms": values[(len(values) - 1) // 2] / 1000,
            "p95_ms": values[int((len(values) - 1) * .95)] / 1000,
            "max_ms": values[-1] / 1000}


def analyze(rows):
    stages, reasons, latencies = Counter(), Counter(), defaultdict(list)
    sessions, last_sequence, order_errors, stopped, dropped = {}, 0, 0, False, None
    cc_state, cc_at, cc_durations = None, None, Counter()
    write_errors, idle_pushed = None, 0
    cc_end, cc_context, cc_uncovered, cc_gaps = None, None, 0, []
    for row in rows:
        if row.get("event") == "capture_stop":
            stopped, dropped = True, row.get("p25_trace_dropped")
            write_errors = row.get("p25_event_write_errors")
            if cc_state is not None and row.get("monotonic_us", 0) >= cc_at:
                cc_durations[cc_state] += row["monotonic_us"] - cc_at
        if row.get("event") != "p25_pipeline":
            continue
        if row.get("schema") != 1:
            raise ValueError("Unsupported pipeline schema")
        stage = row["stage"]
        seq, now = row["trace_seq"], row["monotonic_us"]
        if seq != last_sequence + 1:
            order_errors += 1
        last_sequence = seq
        stages[stage] += 1
        if row.get("reason"):
            reasons[stage + ":" + row["reason"]] += 1
        if stage == "cc_availability":
            if cc_state is not None and now >= cc_at:
                cc_durations[cc_state] += now - cc_at
            cc_state, cc_at = row["reason"], now
            if cc_state != "enabled":
                cc_context, cc_end = None, None
        if stage == "idle_speaker_topup":
            idle_pushed += row.get("pushed_samples", 0)
        if stage == "cc_submitted":
            context = tuple(row.get(k) for k in
                            ("device", "session", "generation", "center_hz", "sample_rate", "target_hz"))
            begin, end, rate = row.get("iq_start", 0), row.get("iq_end", 0), row.get("sample_rate", 0)
            if end > begin and rate > 0:
                if context == cc_context and cc_end is not None and begin > cc_end:
                    cc_uncovered += begin - cc_end
                    cc_gaps.append((begin - cc_end) * 1e6 / rate)
                cc_context, cc_end = context, end
        if stage in ("voice_completed", "cc_completed"):
            for label, begin, end in (("queue", "submitted_us", "started_us"),
                                      ("decode", "started_us", "completed_us")):
                if row.get(begin) and row.get(end, 0) >= row[begin]:
                    latencies[stage.split("_")[0] + "_" + label].append(row[end] - row[begin])
        if stage == "voice_publish" and row.get("reason") == "published":
            if row.get("completed_us") and now >= row["completed_us"]:
                latencies["voice_publication"].append(now - row["completed_us"])
        if stage.startswith("voice_") or stage == "speaker_queue":
            # Call ids can restart when a Receiver is recreated. Never merge
            # different RF allocations just because their local call id repeats.
            key = ':'.join(str(row.get(k, 0)) for k in
                           ("session", "generation", "flush", "target_hz", "slot"))
            s = sessions.setdefault(key, {"tg": row["tg"], "slot": row["slot"],
                "target_hz": row["target_hz"], "jobs": 0, "jobs_without_pcm": 0,
                "selected_vcw": 0, "companion_vcw": 0, "accepted_frames": 0,
                "decoded_samples": 0, "pushed_samples": 0, "rids": set(), "pushes": []})
            if row.get("rid_known"):
                s["rids"].add(row["rid"])
            if stage == "voice_completed":
                s["jobs"] += 1
                s["jobs_without_pcm"] += row.get("pcm_samples", 0) == 0
                for field in ("selected_vcw", "companion_vcw", "accepted_frames"):
                    s[field] += row.get(field, 0)
                s["decoded_samples"] += row.get("pcm_samples", 0)
            if stage == "speaker_queue":
                s["pushed_samples"] += row.get("pushed_samples", 0)
                if row.get("pushed_samples", 0):
                    s["pushes"].append(now)
    for s in sessions.values():
        s["rids"] = sorted(s["rids"])
        times = s.pop("pushes")
        s["push_spacing"] = distribution([b - a for a, b in zip(times, times[1:])])
    return {"schema": 1, "capture_stopped": stopped, "trace_dropped": dropped,
            "event_write_errors": write_errors, "idle_pushed_samples_unattributed": idle_pushed,
            "cc_unsubmitted_samples_while_enabled": cc_uncovered,
            "cc_unsubmitted_gaps": distribution(cc_gaps),
            "sequence_errors": order_errors, "stages": dict(stages), "reasons": dict(reasons),
            "latencies": {k: distribution(v) for k, v in latencies.items()},
            "cc_seconds": {k: v / 1e6 for k, v in cc_durations.items()}, "sessions": sessions,
            "limitations": ["PCM counts and push spacing do not prove intelligibility or speaker continuity.",
                "Correlate with ring-health callback deltas and time-aligned RF before attributing a gap.",
                "Idle top-ups are aggregate only; decoded minus per-job pushed samples is not a loss measurement.",
                "Capture boundaries may contain partial jobs; CC gap counts exclude suspended/retuned intervals.",
                "Unknown RID is not evidence of a new speaker; missing CC intervals hide grant arrival."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("events", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    # Strict final-file parsing: a truncated live tail is incomplete evidence, not a dropped radio frame.
    with args.events.open(encoding="utf-8") as stream:
        report = analyze(json.loads(line) for line in stream if line.strip())
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
