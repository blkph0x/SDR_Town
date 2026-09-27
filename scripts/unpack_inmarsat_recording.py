"""Unpack a bounded diagnostic bundle into SigMF IQ and listenable WAVs."""
import argparse
import base64
import json
from pathlib import Path
import wave

parser=argparse.ArgumentParser()
parser.add_argument("recording",type=Path)
parser.add_argument("output",type=Path,help="new output directory (never overwritten)")
args=parser.parse_args()
if args.recording.stat().st_size>1024*1024:raise SystemExit("Recording exceeds 1 MiB")
value=json.loads(args.recording.read_text())
if value.get("schema")!="sdr-town-inmarsat-recording-v1":raise SystemExit("Unknown schema")
decoded={}
for key,limit,alignment in (("ifBase64",480000,2),("pcmBase64",80000,2),("iqBase64",131072,8)):
    data=base64.b64decode(value[key],validate=True)
    if len(data)>limit or len(data)%alignment:raise SystemExit("Invalid sample count")
    decoded[key]=data
args.output.mkdir(exist_ok=False)
for key,name,rate in (("ifBase64","modem-if.wav",48000),("pcmBase64","decoded-audio.wav",8000)):
    with wave.open(str(args.output/name),"wb") as output:
        output.setnchannels(1);output.setsampwidth(2);output.setframerate(rate);output.writeframes(decoded[key])
(args.output/"source.sigmf-data").write_bytes(decoded["iqBase64"])
metadata={"global":{"core:datatype":"cf32_le","core:sample_rate":value["iqRate"],
                    "core:version":"1.0.0","core:description":"Short original-IQ diagnostic excerpt"},
          "captures":[{"core:sample_start":0,"core:frequency":value["iqCenterHz"]}],"annotations":[]}
(args.output/"source.sigmf-meta").write_text(json.dumps(metadata,indent=2))
print("Unpacked original IQ, modem IF and decoded audio; source IQ is only a short excerpt.")
