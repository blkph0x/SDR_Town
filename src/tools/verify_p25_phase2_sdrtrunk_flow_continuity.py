#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text()
take = main[main.find('std::vector<std::complex<float>> takeUndecoded'):main.find('struct P25AudioResamplerState')]
assert 'Cap fresh tail length while preserving overlap pre-roll' in take, 'missing capped-tail continuity comment'
assert '*outDecodeEndAbsolute = startAbsolute + static_cast<uint64_t>(returnedEnd);' in take, 'takeUndecoded must expose returned chunk end'
assert 'markDecodeSubmitted(uint64_t decodeEndAbsolute)' in main, 'decode cursor should reserve submitted chunks'
assert 'commitDecodeAbsolute(uint64_t decodeEndAbsolute)' in main, 'decode cursor should commit only after worker result'
assert 'phase2IqByRx[p25ReceiverSessionKey(rx)].commitDecodeAbsolute(iqDecodeEndAbsolute)' in main, 'worker result must commit returned chunk end'
assert 'lastDecodeAbsolute = endAbsolute;' not in main[main.find('std::vector<std::complex<float>> takeUndecoded'):main.find('struct P25AudioResamplerState')], 'takeUndecoded still skips to live edge'
assert 'const size_t returnedEnd = (maxSamples > 0)' in take, 'takeUndecoded should cap fresh tail without moving first'
assert 'p25RefreshFollowGrantFromRegistry(armTg, registrySnapshot, armPrepareMs)' in main, 'voice arm must refresh same-call grant metadata before reset'
assert 'P25 voice arm refreshed same-call grant metadata' in main, 'voice arm refresh should be logged for field captures'
assert 'rx.p25VoiceClearKnown = p25TalkgroupGrantProvesSpeakerClear(armTg)' in main, 'voice arm must use refreshed grant clear state'
print('P25 Phase 2 sdrtrunk flow continuity regression: PASS')
