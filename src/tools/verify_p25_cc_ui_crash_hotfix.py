#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text()
header = (root.parent / 'include' / 'P25LiveDecoder.h').read_text()
dec = (root / 'P25LiveDecoder.cpp').read_text()
assert 'bool enablePhase2Decode = true;' in header
assert 'static P25LiveDecoderConfig p25RealtimeControlDecoderConfig()' in main
assert 'P25LiveDecoderConfig cfg = p25DiagnosticDecoderConfig();' in main
assert 'cfg.enableC4fmFixedPhaseSearch = true;' in main
assert 'cfg.maxC4fmFixedPhaseCandidates = 10;' in main
assert 'cfg.maxFrameSyncs = 12;' in main
assert 'cfg.enablePhase2Decode = true;' in main
assert 'cfg.stopCqpskSearchOnHardLock = false;' in main
assert 'P25LiveDecoder p25ControlWorkerDecoder{p25RealtimeControlDecoderConfig()};' in main
assert 'p25ControlWorkerResetPending.store(true' in main
assert 'p25ControlWorkerResetPending.exchange(false' in main
assert 'std::lock_guard<std::mutex> lk(p25ControlWorkerDecoderMutex); p25ControlWorkerDecoder.reset();' not in main
assert 'if (m_config.enablePhase2Decode)' in dec
assert 'if (false && rx.p25VoicePhase2 && p25Phase2NeedsTargetOffsetProbe(live))' in main
print('P25 CC UI crash/freeze hotfix regression: PASS')
