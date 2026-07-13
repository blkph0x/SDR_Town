#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text()
assert 'Do not\n            // jump to the newest maxSamples chunk' in main, 'missing no-jump continuity comment'
assert 'lastDecodeAbsolute = startAbsolute + static_cast<uint64_t>(returnedEnd);' in main, 'cursor must advance only to returned chunk end'
assert 'lastDecodeAbsolute = endAbsolute;' not in main[main.find('std::vector<std::complex<float>> takeUndecoded'):main.find('struct P25AudioResamplerState')], 'takeUndecoded still skips to live edge'
assert 'count = maxSamples;' in main[main.find('std::vector<std::complex<float>> takeUndecoded'):main.find('struct P25AudioResamplerState')], 'takeUndecoded should cap count without moving first'
assert 'p25RefreshFollowGrantFromRegistry(armTg, registrySnapshot, armPrepareMs)' in main, 'voice arm must refresh same-call grant metadata before reset'
assert 'P25 voice arm refreshed same-call grant metadata' in main, 'voice arm refresh should be logged for field captures'
assert 'rx.p25VoiceClearKnown = p25TalkgroupGrantProvesSpeakerClear(armTg)' in main, 'voice arm must use refreshed grant clear state'
print('P25 Phase 2 sdrtrunk flow continuity regression: PASS')
