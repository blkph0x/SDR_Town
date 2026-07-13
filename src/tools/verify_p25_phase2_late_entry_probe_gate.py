#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
required = [
    'const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;',
    'if (!epochTrusted && !grantMayProbeVoice)',
    'if (!epochTrusted && grantMayProbeVoice)',
    'if (burst.grantSlotKnown && static_cast<uint8_t>(burst.grantSlot & 0x01u) != followedGrantSlot)',
    'if (!burst.grantSlotKnown && !grantMayProbeVoice)',
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
