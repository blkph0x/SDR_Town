"""Collector abuse regression tests; only temporary files and localhost."""
import importlib.util
import base64
import json
from pathlib import Path
import tempfile
import threading
import unittest
import time
from http.cookiejar import CookieJar
from urllib.request import build_opener, HTTPCookieProcessor
from urllib.parse import urlencode
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
    def test_event_history_filters_retention_and_migration(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);legacy=event();legacy["type"]="app.system"
            (root/"old.jsonl").write_text(json.dumps(legacy)+"\n",encoding="utf-8")
            state=collector.DiagnosticsState(root,"ingest","admin",49152)
            self.assertEqual(state.list_events()["indexedEvents"],1)
            self.assertEqual(state.list_clients(),[]) # Backfill must not re-count installations/issues.
            for i in range(6):
                row=event();row["seq"]=str(i);row["clientId"]="second" if i%2 else "first"
                row["type"]="ui.actions";state.write_event(row)
            page=state.list_events(client="first",event_type="ui.actions",limit=2)
            self.assertEqual([x["event"]["seq"] for x in page["events"]],["4","2"])
            older=state.list_events(client="first",event_type="ui.actions",before=page["nextBefore"])
            self.assertEqual(older["events"][0]["event"]["seq"],"0")
            self.assertEqual(state.list_events(client="' OR 1=1 --")["events"],[])
            self.assertEqual(len(state.list_events(limit=10000)["events"]),7)
            state=collector.DiagnosticsState(root,"ingest","admin",49152)
            self.assertEqual(state.list_events()["indexedEvents"],7)
            with state._connect() as con: con.execute("UPDATE events SET received=0 WHERE event_type='app.system'")
            state.expire_events();self.assertEqual(state.list_events()["indexedEvents"],6)
            self.assertEqual(sum(x["report_count"] for x in state.list_clients()),6)

    def test_admin_event_dashboard_cookie_auth_and_escaping(self):
        with tempfile.TemporaryDirectory() as d:
            state=collector.DiagnosticsState(Path(d),"ingest","admin",49152)
            e=event();e["payload"]={"message":"<script>alert('bad')</script>"};state.write_event(e)
            server=collector.DiagnosticsServer(("127.0.0.1",0),state)
            thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            base=f"http://127.0.0.1:{server.server_port}"
            opener=build_opener(HTTPCookieProcessor(CookieJar()))
            try:
                for path in ("/api/events","/admin/events"):
                    with self.assertRaises(HTTPError) as caught:
                        urlopen(Request(base+path,headers={"Authorization":"Bearer ingest"}),timeout=3)
                    self.assertEqual(caught.exception.code,401)
                login=opener.open(Request(base+"/admin/login",data=urlencode({"token":"admin"}).encode()),timeout=3)
                self.assertEqual(login.geturl(),base+"/admin");login.close()
                with opener.open(base+"/admin/events",timeout=3) as response:
                    page=response.read().decode();self.assertIn("&lt;script&gt;",page)
                    self.assertNotIn("<script>",page);self.assertEqual(response.headers["Referrer-Policy"],"no-referrer")
                with opener.open(base+"/api/events?type=app.performance.sample",timeout=3) as response:
                    self.assertEqual(len(json.load(response)["events"]),1)
                with self.assertRaises(HTTPError) as caught: opener.open(base+"/api/events?before=bad",timeout=3)
                self.assertEqual(caught.exception.code,400)
                state.admin_token="rotated"
                with self.assertRaises(HTTPError) as caught: opener.open(base+"/api/events",timeout=3)
                self.assertEqual(caught.exception.code,401)
            finally:server.shutdown();server.server_close();thread.join(3)

    def test_recording_validation_and_durable_quota(self):
        value=dict(schema="sdr-town-inmarsat-recording-v1", channelHz=1545000000,
                   mode=3, timeUtc="2026-09-27T00:00:00Z", ifRate=48000, pcmRate=8000,
                   format="s16le", ifBase64=base64.b64encode(bytes(480000)).decode(),
                   pcmBase64="", clientId="test-install", version="0.2.112", before={}, after={},
                   iqBase64=base64.b64encode(bytes(131072)).decode(),iqRate=2400000,
                   iqCenterHz=1545000000,iqStartSample="0",iqFormat="cf32_le")
        self.assertTrue(collector.valid_recording(value))
        for key, bad in (("mode", 4), ("channelHz", float("nan")),
                         ("ifBase64", "garbage"), ("clientId", "../bad"),
                         ("pcmBase64", base64.b64encode(bytes(80002)).decode())):
            broken=dict(value);broken[key]=bad;self.assertFalse(collector.valid_recording(broken))
        with tempfile.TemporaryDirectory() as d:
            state=collector.DiagnosticsState(Path(d), "ingest", "admin", 49152)
            for _ in range(4):self.assertIsNotNone(state.save_recording(value))
            self.assertIsNone(state.save_recording(value))
            state=collector.DiagnosticsState(Path(d), "ingest", "admin", 49152)
            self.assertIsNone(state.save_recording(value))
            self.assertEqual(len(list((Path(d)/"recordings").glob("*.json"))),4)
            with state._connect() as con:
                con.execute("UPDATE recordings SET created=0")
            state.expire_recordings()
            self.assertEqual(len(list((Path(d)/"recordings").glob("*.json"))),0)
            for _ in range(16):self.assertTrue(state.allow_recording_request())
            self.assertFalse(state.allow_recording_request())

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
                recording=dict(schema="sdr-town-inmarsat-recording-v1",channelHz=1545000000,
                    mode=3,timeUtc="2026-09-27T00:00:00Z",ifRate=48000,pcmRate=8000,
                    format="s16le",ifBase64=base64.b64encode(bytes(480000)).decode(),
                    pcmBase64="",before={},after={},clientId="fixture-client",version="0.2.112",
                    iqBase64=base64.b64encode(bytes(131072)).decode(),iqRate=2400000,
                    iqCenterHz=1545000000,iqStartSample="0",iqFormat="cf32_le")
                # Authorization precedes body reads. Keep the unauthorized probe
                # small: sending 1 MiB into a closed rejected connection can
                # surface Winsock 10053 instead of the already-sent HTTP 401.
                self.assertEqual(request("/recordings", "wrong", {}),401)
                self.assertEqual(request("/recordings", "ingest", {"bad":True}),400)
                self.assertEqual(request("/recordings", "ingest", recording),201)
                self.assertEqual(request("/api/recordings", "ingest"),401)
                self.assertEqual(request("/api/recordings", "admin"),200)
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
