"""Verify release configuration without real credentials or network access."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PackagingTests(unittest.TestCase):
    def test_defaults_and_required_credential(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d) / "remote_diagnostics.defaults.json"
            env = dict(os.environ); env.pop("SDR_TOWN_DIAG_RELEASE_TOKEN", None)
            command = ["cmake", f"-DTEMPLATE={ROOT / 'config/diagnostics-defaults.json'}",
                       f"-DOUTPUT={output}", "-DREQUIRE_TOKEN=ON", "-P", str(ROOT / "cmake/StageDiagnostics.cmake")]
            self.assertNotEqual(subprocess.run(command, env=env, capture_output=True).returncode, 0)
            self.assertFalse(output.exists())
            env["SDR_TOWN_DIAG_RELEASE_TOKEN"] = "test-only-not-a-real-token-123456"
            run = subprocess.run(command, env=env, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr.decode())
            self.assertNotIn(env["SDR_TOWN_DIAG_RELEASE_TOKEN"].encode(), run.stdout + run.stderr)
            config = json.loads(output.read_text())
            self.assertIs(config["enabled"], False)
            self.assertEqual(config["url"], "https://gearsqueens.online/sdr-town-diag/ingest")
            self.assertEqual(config["token"], env["SDR_TOWN_DIAG_RELEASE_TOKEN"])
            env["SDR_TOWN_DIAG_RELEASE_TOKEN"] = 'invalid"credential'
            self.assertNotEqual(subprocess.run(command, env=env, capture_output=True).returncode, 0)


if __name__ == "__main__": unittest.main()
