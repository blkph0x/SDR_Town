#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
required = [
    'const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;',
    'if (!epochTrusted && !grantMayProbeVoice && !forceEstablishedFeed)',
    'if (!epochTrusted && grantMayProbeVoice)',
    'const uint8_t effectiveBurstSlot = burst.grantSlotKnown',
    'if (burst.grantSlotKnown && effectiveBurstSlot != followedGrantSlot)',
    'if (!burst.grantSlotKnown && !grantMayProbeVoice && !forceEstablishedFeed)',
    'if (!burst.grantSlotKnown && grantMayProbeVoice)',
    'if (acceptedVoice) {',
    'out.phase2AudioLockMissing = false;',
    'out.phase2MetadataMissing = false;',
    'out.phase2MaskMissing = false;',
    'late-entry-vocoder-probe-active',
    'applyP25Phase2SecurityAudioGate',
]
for needle in required:
    assert needle in main, f'missing late-entry probe/security gate fix: {needle}'
bad = """const bool epochTrusted = burst.superframeLock || burst.macCrcLock || burst.sessionAudioRelease;\n        if (!epochTrusted) {\n            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();"""
assert bad not in main, 'stale unconditional epoch gate still rejects late-entry voice codewords'
print('P25 Phase 2 late-entry probe gate regression: PASS')
