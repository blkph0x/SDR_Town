from pathlib import Path
root = Path(__file__).resolve().parents[1]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
checks = [
    ('stale encrypted history follows traffic but keeps speaker gated',
     'Preserve sticky clear, but do not let stale encrypted registry history' in main and
     'probingUnknownPhase2EncryptedHistory = true' in main),
    ('current-call encrypted hold still present', 'gP25RecentExplicitEncryptedPhase2Grants' in main or 'same TG/channel/frequency' in main),
    ('sdrtrunk queue/PTT/ESS gate present', 'applyP25Phase2SecurityAudioGate' in main and 'P25P2CallAudioKey' in main and 'p25QueuePhase2PendingAmbeFrame' in main),
    ('unknown speaker audio is queued', 'waitingForClearGrant = true' in main and 'unknown-waiting-clear' in main),
    ('no speculative VCW/mask speaker release', 'strong-target-vcw-mask-direct-release' not in main and 'strong-target-vcw-mask-pending-release' not in main),
    ('old speculative encrypted-history probe log removed', 'Auto-follow probing Phase 2 TG %1 even though previous TG history says encrypted' not in main),
]
failed=[name for name,ok in checks if not ok]
if failed:
    print('FAIL')
    for f in failed: print(' -', f)
    raise SystemExit(1)
print('sdrtrunk-like Phase 2 encryption/security gate: PASS')
