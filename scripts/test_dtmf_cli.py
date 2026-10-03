#!/usr/bin/env python3
"""Independent PCM fixtures exercise the shipped DTMF CLI contract (DEC-0168)."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


def recording(path, pairs, *, duration=.020, gap=.015, inverse=False, scale=1., shift=0.):
    data=[]
    for row, col in pairs:
        row, col=row*scale+shift,col*scale+shift
        if inverse:
            row,col=3300-row,3300-col
        data.extend(int(-6000*(math.sin(2*math.pi*row*i/8000)+math.sin(2*math.pi*col*i/8000)))
                    for i in range(round(duration*8000)))
        data.extend([0]*round(gap*8000))
    with wave.open(str(path),'wb') as out:
        out.setnchannels(1);out.setsampwidth(2);out.setframerate(8000)
        out.writeframes(struct.pack('<'+'h'*len(data),*data))
    return len(data)


def run(exe, path, flags='', error=False):
    result=subprocess.run([str(exe),'--allow-multiple','--cli','--no-control-server',
        '--no-remote-diagnostics','--cmd',f'tones dtmf "{path}" {flags}'],
        capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45)
    output=result.stdout+'\n'+result.stderr
    if error:
        assert 'Tone error:' in output, output
        return
    for line in result.stdout.splitlines():
        try:
            value=json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value,dict) and value.get('decoder')=='dtmf':
            return value
    raise AssertionError(output[-5000:])


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True)
    args=parser.parse_args();exe=Path(args.exe).resolve()
    pairs=[(697,1209),(697,1477),(697,1477),(852,1209)]
    with tempfile.TemporaryDirectory(prefix='dtmf cli ') as root:
        path=Path(root)/'DTMF \u00e9 burst.wav'
        count=recording(path,pairs)
        fast=run(exe,path,'--fast')
        assert fast['lastSequence']=='1337' and fast['confirmedDigits']==4,fast
        assert fast['samples']==count and fast['options']['profile']=='fast',fast
        assert len(fast['detections'])==4 and fast['detections'][0]['sampleRate']==8000,fast
        conservative=run(exe,path)
        assert conservative['confirmedDigits']==0,conservative
        count=recording(path,pairs,inverse=True,gap=0.015)
        inverse=run(exe,path,'--fast --invert-hz 3300')
        assert inverse['lastSequence']=='1337' and inverse['samples']==count,inverse
        assert run(exe,path,'--fast')['confirmedDigits']==0
        recording(path,[(770,1336)],duration=.050,gap=0,scale=1.12,shift=-120)
        shifted=run(exe,path,'--scale 1.12 --shift-hz -120')
        assert shifted['lastSequence']=='5' and shifted['samples']==400,shifted
        run(exe,path,'--fast --unknown',error=True)
        run(exe,path,'--scale nope',error=True)
        run(exe,path,'--invert-hz 6000',error=True)
        run(exe,path,'--shift-hz',error=True)
    print('DTMF CLI PASS: short/repeated/polarity/inverted/shifted/EOF/Unicode/invalid options')


if __name__=='__main__':
    main()
