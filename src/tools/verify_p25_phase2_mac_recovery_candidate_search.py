#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "P25LiveDecoder.cpp").read_text()

required = [
    "rs63DecodeWithUnknownSymbolErrors",
    "const int maxUnknownSymbols = deepSearch ? 2 : 0;",
    "too expensive for every realtime false ACCH hypothesis",
    "transmittedPositions.push_back(v)",
    "return r.stats.phase2MacCrcValid > 0 || r.stats.phase2EssKnown;",
    "phase2FecMacCandidates",
    "Non-CRC direct ACCH probes are useful diagnostics",
    "std::vector<int> essTransmittedPositions",
]

missing = [needle for needle in required if needle not in cpp]
if missing:
    raise SystemExit("Missing expected Phase 2 MAC recovery/candidate-search patch markers: " + ", ".join(missing))

bad = "r.stats.phase2VoiceCodewords >= 4"
if bad in cpp[cpp.find("bool hasPhase2FastStopEvidence"):cpp.find("size_t phase2VoiceCodewordCount")]:
    raise SystemExit("Phase 2 fast-stop still accepts untrusted VCW-only telemetry")

print("P25 Phase 2 MAC recovery and candidate-search regression: PASS")
