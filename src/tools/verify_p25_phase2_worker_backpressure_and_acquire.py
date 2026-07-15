#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8')
decoder = (root / 'src' / 'P25LiveDecoder.cpp').read_text(encoding='utf-8')
header = (root / 'include' / 'P25LiveDecoder.h').read_text(encoding='utf-8')

acquire_match = re.search(r'kP25Phase2VoiceDecodeAcquireChunkSeconds\s*=\s*([0-9.]+)', main)
acquire_seconds = float(acquire_match.group(1)) if acquire_match else 999.0

checks = {
    'worker accept blocks while busy': 'p25VoiceWorkerBusy.load(std::memory_order_acquire)' in main and
        'p25VoiceWorkerCanAcceptJob' in main,
    'worker dsp mutex wait instead of infinite block': 'dsp-mutex-timeout' in main and
        'kP25VoiceWorkerDspMutexWaitMs' in main,
    'smaller live acquire chunk': 0.0 < acquire_seconds <= 0.080,
    'skip offset probe when one-rtl centered': '!oneRtlPhysicallyOnVoice' in main and 'coldAcquireWindow' in main,
    'ambe msb-first dibit packing': 'cw.bits[d * 2] = bits[0];' in decoder and 'cw.bits[d * 2 + 1] = bits[1];' in decoder,
    'contiguous phase2 timeslot no status strip': (
        'Phase 2 TDMA does not' in decoder and
        'stripPhase2TimeslotPayload' not in decoder and
        'm_phase2StatusStripPeriodKnown' not in header
    ),
    'voicetest mask auto-seed from capture log': 'trySeedP25ReplayMaskFromCaptureLog' in main,
    'decoder realtime budget setter': 'setRealtimeDecodeBudgetMs' in header,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('verify_p25_phase2_worker_backpressure_and_acquire failed:\n- ' + '\n- '.join(failed))
print('verify_p25_phase2_worker_backpressure_and_acquire: PASS')
