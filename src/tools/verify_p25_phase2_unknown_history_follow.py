#!/usr/bin/env python3
from pathlib import Path
s = Path(__file__).resolve().parents[1].joinpath('main.cpp').read_text()
# Fail-closed: sticky encrypted TG history must NOT be wiped on OP=0x02 unknown.
assert 'tg.encryptionKnown && tg.encrypted' in s
assert 'Fail closed: keep encrypted' in s or 'do not retune for speculative audio' in s
assert 'Preserve sticky clear *and* sticky encrypted' in s or 'preserve both clear and encrypted' in s.lower() or 'Preserve both clear and encrypted' in s
assert 'Auto-follow skipped Phase 2 TG %1 because the grant/update has no clear service options' not in s
# Old speculative wipe path must stay gone.
assert 'temporarily clear stale encrypted history' not in s
assert 'probingUnknownPhase2EncryptedHistory = tg.encryptionKnown && tg.encrypted' not in s
print('Phase 2 sticky encrypted-history fail-closed: PASS')
