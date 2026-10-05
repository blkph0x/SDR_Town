import unittest
from analyze_p25_timeline import analyze


class TimelineTests(unittest.TestCase):
    def test_correlated_latency_and_no_fake_speech_claim(self):
        base = {"event": "p25_pipeline", "schema": 1, "session": 7, "tg": 10,
                "slot": 0, "target_hz": 420100000, "submitted_us": 1000,
                "started_us": 2000, "completed_us": 5000, "reason": ""}
        rows = [dict(base, trace_seq=1, monotonic_us=5000, stage="voice_completed",
                     pcm_samples=160, selected_vcw=1, accepted_frames=1),
                dict(base, trace_seq=2, monotonic_us=6000, stage="voice_publish", reason="published"),
                {"event": "capture_stop", "p25_trace_dropped": 0, "monotonic_us": 7000}]
        result = analyze(rows)
        self.assertEqual(result["latencies"]["voice_queue"]["p50_ms"], 1)
        self.assertEqual(result["latencies"]["voice_decode"]["p50_ms"], 3)
        self.assertEqual(result["latencies"]["voice_publication"]["p50_ms"], 1)
        session = next(iter(result["sessions"].values()))
        self.assertEqual(session["pushed_samples"], 0)
        self.assertEqual(session["rids"], [])
        self.assertTrue(result["capture_stopped"])

    def test_loss_and_incomplete_capture_are_not_success(self):
        result = analyze([])
        self.assertFalse(result["capture_stopped"])
        self.assertIsNone(result["trace_dropped"])
        row = {"event": "p25_pipeline", "schema": 1, "stage": "cc_availability",
               "reason": "traffic-follow-suspended", "trace_seq": 2, "monotonic_us": 1000}
        result = analyze([row, {"event": "capture_stop", "monotonic_us": 2001000,
                                "p25_trace_dropped": 3}])
        self.assertEqual(result["sequence_errors"], 1)
        self.assertEqual(result["trace_dropped"], 3)
        self.assertEqual(result["cc_seconds"]["traffic-follow-suspended"], 2)

    def test_receiver_recreation_does_not_merge_calls(self):
        row = {"event": "p25_pipeline", "schema": 1, "session": 7, "tg": 10,
               "slot": 0, "target_hz": 420100000, "stage": "voice_started",
               "trace_seq": 1, "monotonic_us": 1000, "generation": 1}
        later = dict(row, trace_seq=2, monotonic_us=5000, generation=2)
        self.assertEqual(len(analyze([row, later])["sessions"]), 2)

    def test_cc_coverage_excludes_retunes_and_suspension(self):
        base = {"event": "p25_pipeline", "schema": 1, "stage": "cc_submitted",
                "session": 1, "monotonic_us": 1, "sample_rate": 1000}
        rows = [dict(base, trace_seq=1, iq_start=0, iq_end=256),
                dict(base, trace_seq=2, iq_start=356, iq_end=612),
                dict(base, trace_seq=3, iq_start=800, iq_end=1000, session=2),
                dict(base, trace_seq=4, stage="cc_availability", reason="traffic-follow-suspended"),
                dict(base, trace_seq=5, iq_start=1500, iq_end=1756, session=2)]
        result = analyze(rows)
        self.assertEqual(result["cc_unsubmitted_samples_while_enabled"], 100)
        self.assertEqual(result["cc_unsubmitted_gaps"]["count"], 1)
        self.assertEqual(result["cc_unsubmitted_gaps"]["max_ms"], 100)

    def test_idle_push_and_write_failures_are_not_hidden(self):
        result = analyze([{"event": "p25_pipeline", "schema": 1, "stage": "idle_speaker_topup",
                           "trace_seq": 1, "monotonic_us": 1, "pushed_samples": 960},
                          {"event": "capture_stop", "p25_event_write_errors": 2}])
        self.assertEqual(result["idle_pushed_samples_unattributed"], 960)
        self.assertEqual(result["event_write_errors"], 2)


if __name__ == "__main__":
    unittest.main()
