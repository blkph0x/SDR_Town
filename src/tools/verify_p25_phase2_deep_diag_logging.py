#!/usr/bin/env python3
from pathlib import Path
src = Path(__file__).resolve().parents[1] / 'main.cpp'
text = src.read_text(encoding='utf-8')
required = [
    'TDMA DEEP DIAG:',
    'state{decode=%2 phase2=%3 clearKnown=%4 encrypted=%5 callClearTrusted=%6 unknownProbe=%7',
    'rf{voice=%19MHz cf=%20MHz offsetHz=%21 inPassband=%22 sr=%23MHz',
    'live{diag=%26 sync=%27 nid=%28 nidLock=%29 imbe=%30 decoded=%31 audioSamples=%32 backend=%33',
    'gates{vcwPresent=%42 sfLocked=%43 maskLocked=%44 macTrusted=%45 essTrusted=%46 block=%47}',
    'while (p25LogLines.size() > 1500) p25LogLines.removeFirst();',
    'while (p25VisibleLogPending.size() > 600) p25VisibleLogPending.removeFirst();',
    'if (acqNowMs - lastTdmaAcqStatusMs > 1000) {'
]
missing = [r for r in required if r not in text]
if missing:
    raise SystemExit('missing expected deep diagnostic instrumentation: ' + ', '.join(missing))
print('P25 Phase 2 deep diagnostic logging regression: PASS')
