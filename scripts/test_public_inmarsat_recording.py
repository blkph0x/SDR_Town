"""Explicit synthetic HTTPS collector test. Never sends private recordings."""
import argparse
import base64
import json
from pathlib import Path
from urllib.request import Request,urlopen
import uuid

parser=argparse.ArgumentParser()
parser.add_argument("--config",required=True,type=Path)
parser.add_argument("--admin-token-file",required=True,type=Path)
parser.add_argument("--admin-base",default="http://127.0.0.1:8787")
args=parser.parse_args()
config=json.loads(args.config.read_text(encoding="utf-8-sig"))
endpoint=config["url"]
if not endpoint.startswith("https://") or not endpoint.endswith("/ingest"):
    raise SystemExit("HTTPS collector required")
base=endpoint[:-7]
value=dict(schema="sdr-town-inmarsat-recording-v1",channelHz=1545000000,
    mode=3,timeUtc="2026-09-27T00:00:00Z",ifRate=48000,pcmRate=8000,format="s16le",
    ifBase64=base64.b64encode(bytes(480000)).decode(),pcmBase64="",before={},after={},
    iqBase64=base64.b64encode(bytes(131072)).decode(),iqRate=2400000,iqCenterHz=1545000000,
    iqStartSample="0",iqFormat="cf32_le",clientId="synthetic-"+uuid.uuid4().hex,version="0.2.112")
def request(url,token,body=None):
    req=Request(url,data=None if body is None else json.dumps(body).encode(),
        headers={"Content-Type":"application/json","Authorization":"Bearer "+token})
    with urlopen(req,timeout=30) as response:return response.status,json.load(response)
status,receipt=request(base+"/recordings",config["token"],value)
assert status==201 and receipt["ok"]
if args.admin_base!="http://127.0.0.1:8787":raise SystemExit("Admin test is local only")
status,stored=request(args.admin_base+"/api/recordings?id="+receipt["recordingId"],args.admin_token_file.read_text().strip())
assert status==200 and stored==value
print("PASS synthetic public HTTPS upload and exact local admin retrieval; receipt="+receipt["recordingId"])
