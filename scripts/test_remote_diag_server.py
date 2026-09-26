"""Collector abuse regression tests; only temporary files and localhost."""
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.request import Request, urlopen
from urllib.error import HTTPError

spec = importlib.util.spec_from_file_location(
    "collector", Path(__file__).resolve().parents[1] / "src/tools/remote_diag_server.py")
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


def event():
    return dict(schema="sdr-town-remote-diagnostics-v1", app="SDR_Town",
                clientId="fixture-client", sessionId="fixture-session",
                type="app.performance.sample", severity="info", payload={"workingSetBytes": 10})


class CollectorTests(unittest.TestCase):
    def test_validation(self):
        self.assertTrue(collector.valid_event(event()))
        for field, value in (("app", "junk"), ("clientId", "../escape"),
                             ("schema", None), ("payload", []), ("type", "bad\nvalue")):
            bad = event(); bad[field] = value
            self.assertFalse(collector.valid_event(bad))
        for value in (float("nan"), float("inf"), "x" * 16385, list(range(513))):
            bad = event(); bad["payload"] = {"value": value}
            self.assertFalse(collector.valid_event(bad))
        bad = event(); nested = bad["payload"]
        for _ in range(13):
            nested["next"] = {}; nested = nested["next"]
        self.assertFalse(collector.valid_event(bad))

    def test_limits(self):
        with tempfile.TemporaryDirectory() as d:
            state = collector.DiagnosticsState(Path(d), "ingest", "admin", 49152)
            for _ in range(120):
                self.assertTrue(state.allow_event("one", 1))
            self.assertFalse(state.allow_event("one", 1))
            self.assertFalse(state.allow_event("two", 128 * 1024 + 1))
            state.rate_bytes = 16 * 1024 * 1024
            self.assertFalse(state.allow_event("new-id", 1))
            state.rate_window -= 61
            self.assertTrue(state.allow_event("one", 1))

    def test_http_authority_validation_and_receipt(self):
        with tempfile.TemporaryDirectory() as d:
            state = collector.DiagnosticsState(Path(d), "ingest", "admin", 49152)
            server = collector.DiagnosticsServer(("127.0.0.1", 0), state)
            thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
            def request(path, token, body=None):
                req = Request(f"http://127.0.0.1:{server.server_port}{path}",
                              data=None if body is None else json.dumps(body).encode(),
                              headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"})
                try:
                    with urlopen(req, timeout=3) as response: return response.status
                except HTTPError as exc: return exc.code
            try:
                self.assertEqual(request("/ingest", "wrong", event()), 401)
                self.assertEqual(request("/api/issues", "ingest"), 401)
                self.assertEqual(request("/api/issues", "admin"), 200)
                self.assertEqual(request("/ingest", "ingest", {"junk": True}), 400)
                self.assertEqual(request("/ingest", "ingest", event()), 202)
                self.assertTrue((Path(d) / "fixture-session.jsonl").is_file())
                state.rate_bytes = 16 * 1024 * 1024
                self.assertEqual(request("/ingest", "ingest", event()), 429)
                state.admin_token = None
                self.assertEqual(request("/api/issues", "ingest"), 401)
                state.token = None
                self.assertEqual(request("/ingest", "", event()), 401)
            finally:
                server.shutdown(); server.server_close(); thread.join(3)


if __name__ == "__main__":
    unittest.main()
