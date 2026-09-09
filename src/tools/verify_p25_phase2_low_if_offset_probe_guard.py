from pathlib import Path
from p25_orchestration_sources import orchestration_source_text

ROOT = Path(__file__).resolve().parents[2]
main = orchestration_source_text()

assert "oneRtlLowIfTrafficSource" in main, "decode path must identify one-RTL low-IF traffic follows"
assert "!verifiedTrafficTargetOffset && !oneRtlLowIfTrafficSource" in main, (
    "unverified CC/AFC offset must not directly bias low-IF traffic target"
)
assert "preferNominalBeforeUnverifiedOffset" in main, (
    "low-IF unverified offset probing must evaluate nominal grant center before AFC hints"
)
assert "candidateFollowedSlotTelemetry" in main, (
    "unverified offset candidates must require followed-slot traffic evidence before winning"
)

nominal_idx = main.index("if (preferNominalBeforeUnverifiedOffset)")
afc_idx = main.index("std::isfinite(rx.p25FrozenAfcOffsetHz)", nominal_idx)
assert nominal_idx < afc_idx, "nominal grant target must be queued before AFC probes"

print("verify_p25_phase2_low_if_offset_probe_guard: OK")
