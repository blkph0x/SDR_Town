#!/usr/bin/env python3
from pathlib import Path
from p25_orchestration_sources import orchestration_source_text
src_text = orchestration_source_text()
text = src_text
required = [
    'TDMA DEEP DIAG:',
    'state{decode=%2 phase2=%3 clearKnown=%4 encrypted=%5 callClearTrusted=%6 unknownProbe=%7',
    'rf{voice=%19MHz cf=%20MHz offsetHz=%21 inPassband=%22 sr=%23MHz',
    'live{diag=%26 sync=%27 nid=%28 nidLock=%29 imbe=%30 decoded=%31 audioSamples=%32 backend=%33',
    'gates{vcwPresent=%46 sfLocked=%47 maskLocked=%48 macTrusted=%49 essTrusted=%50 block=%51}',
    'P25 DSP VOICE WORKER: rolling=%1 iq=%2 fresh=%3 context=%4 absStart=%5',
    'gate=%10 backend=%11 sync=%12 nid=%13 decoded=%14 audio=%15 speaker=%16',
    'gaps=%24 ctxVcw=%25 ctxDrop=%26 pendingQueued=%27 reject=%28 wrongSlot=%29 dup=%30 absDup=%31 seqDrop=%32',
    'lastAbs=%33 p2sf=%34 p2mask=%35 p2mac=%36/%37 %38 ess=%39 dsp=%40us qDrop=%41 rDrop=%42',
    'while (p25LogLines.size() > 1500) p25LogLines.removeFirst();',
    'while (p25VisibleLogPending.size() > 600) p25VisibleLogPending.removeFirst();',
    'if (acqNowMs - lastTdmaAcqStatusMs > 1000) {',
    'lastFinalSecurityGateWriteMs',
    'record["postSecurityGateRecord"] = finalSecurityGateRecord;',
    'writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);'
]
missing = [r for r in required if r not in text]
if missing:
    raise SystemExit('missing expected deep diagnostic instrumentation: ' + ', '.join(missing))
if text.count('writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);') < 4:
    raise SystemExit('Phase-2 selected-slot validation must preserve AMBE frame evidence on reject/partial paths')
print('P25 Phase 2 deep diagnostic logging regression: PASS')
