"""Synthetic modem-recording reproduction, not over-air voice acceptance."""
import argparse
import base64
import json
from pathlib import Path
import subprocess
import tempfile
import sys

parser=argparse.ArgumentParser()
parser.add_argument("--exe",required=True)
args=parser.parse_args()
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    clip=dict(schema="sdr-town-inmarsat-recording-v1", channelHz=1545000000,
              mode=3, timeUtc="2026-09-27T00:00:00Z", ifRate=48000, pcmRate=8000,
              format="s16le", ifBase64=base64.b64encode(bytes(480000)).decode(),
              pcmBase64="", before={}, after={},iqBase64=base64.b64encode(bytes(131072)).decode(),
              iqRate=2400000,iqCenterHz=1545000000,iqStartSample="0",iqFormat="cf32_le")
    path=root/"test.inmarsat.json";path.write_text(json.dumps(clip))
    output=root/"result.json"
    command=[args.exe,"--allow-multiple","--cli","--no-remote-diagnostics",
             "--inmarsat-iq",str(path),"--inmarsat-format","diagnostic_if",
             "--inmarsat-result",str(output),"--inmarsat-wav",str(root/"voice.wav")]
    run=subprocess.run(command,capture_output=True,timeout=30)
    assert run.returncode==0,run.stderr.decode(errors="replace")
    report=json.loads(output.read_text())
    assert report["input48k"]==240000 and report["coldStart"]
    assert report["pcmSamples"]==0 and not report["continuousAudioVerified"]
    for key in ("acarsAirToGround","acarsGroundToAir","acarsUnknownDirection","adscDecoded",
                "positionReports","positionIdentityMismatches","applicationDecoded","applicationInvalid"):
        assert report[key]==0,(key,report[key])
    subprocess.run([sys.executable,str(Path(__file__).with_name("unpack_inmarsat_recording.py")),str(path),str(root/"unpacked")],check=True,timeout=10)
    assert (root/"unpacked/source.sigmf-data").stat().st_size==131072
    clip["ifBase64"]="invalid";path.write_text(json.dumps(clip))
    run=subprocess.run(command,capture_output=True,timeout=30)
    assert run.returncode!=0
print("PASS: bounded IF reproduction, no invented silence voice, malformed rejection")
