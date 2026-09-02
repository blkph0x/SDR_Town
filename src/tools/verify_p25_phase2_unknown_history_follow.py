#!/usr/bin/env python3
from pathlib import Path
s = Path(__file__).resolve().parents[1].joinpath('main.cpp').read_text(encoding='utf-8', errors='replace')
# Stale encrypted TG history must not be allowed to open audio, but it also
# must not prevent following the current Phase-2 allocation for MAC/ESS proof.
assert 'tg.encryptionKnown && tg.encrypted' in s
assert 'probingUnknownPhase2EncryptedHistory = true' in s
assert 'tg.encryptionKnown = false' in s
assert 'tg.encrypted = false' in s
assert 'Preserve sticky clear, but do not let stale encrypted registry history' in s
assert 'Auto-follow skipped Phase 2 TG %1 because the grant/update has no clear service options' not in s
# Old speculative speaker-open path must stay gone.
assert 'temporarily clear stale encrypted history' not in s
assert 'current-call hold' in s and 'target-slot traffic MAC/ESS/PTT' in s
print('Phase 2 sticky encrypted-history traffic probe gate: PASS')

