from pathlib import Path
from p25_orchestration_sources import orchestration_source_text


ROOT = Path(__file__).resolve().parents[2]
decoder = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
main = orchestration_source_text()

required = {
    "normalize stamps burst stream coordinate before offset normalization":
        "const uint64_t burstStreamDibit =\n            phase2WorkingStreamStart + static_cast<uint64_t>(burst.dibitOffset);" in decoder,
    "normalize preserves burst stream coordinate":
        "burst.streamBurstStartDibitKnown = true;\n        burst.streamBurstStartDibit = burstStreamDibit;" in decoder,
    "normalize stamps codeword stream coordinate before clamping":
        "cw.streamDibit = phase2WorkingStreamStart + static_cast<uint64_t>(cw.dibitOffset);" in decoder,
    "normalize preserves codeword burst coordinate":
        "cw.streamBurstStartDibitKnown = true;\n            cw.streamBurstStartDibit = burstStreamDibit;" in decoder,
    "persistent framer stamps burst-local codewords into stream coordinates":
        "cw.streamDibit = streamBurstStart + static_cast<uint64_t>(cw.dibitOffset);" in decoder,
    "persistent framer preserves codeword burst start before annotation":
        "cw.streamBurstStartDibitKnown = true;\n            cw.streamBurstStartDibit = streamBurstStart;" in decoder,
    "persistent framer retains overflow bursts instead of dropping capped live audio":
        "deferredBursts.reserve(pending.end() - split);" in decoder and
        "m_pendingFramerBursts.insert(m_pendingFramerBursts.begin()" in decoder and
        "std::make_move_iterator(deferredBursts.begin())" in decoder,
    "annotator honors stamped codeword coordinate":
        "const uint64_t streamDibit = codeword.streamDibitKnown\n                ? codeword.streamDibit\n                : (streamStart + static_cast<uint64_t>(codeword.dibitOffset));" in decoder,
    "annotator does not double-add burst coordinate":
        "(streamBurstStart + static_cast<uint64_t>(codeword.dibitOffset))" not in decoder,
    "annotator preserves stamped codeword burst coordinate":
        "const bool codewordBurstStartKnown = codeword.streamBurstStartDibitKnown;\n            const uint64_t codewordBurstStart = codewordBurstStartKnown\n                ? codeword.streamBurstStartDibit\n                : streamBurstStart;" in decoder,
    "audio feed orders bursts by monotonic stream coordinate":
        "std::stable_sort(orderedBurstsForFeed.begin(), orderedBurstsForFeed.end()" in main and
        "a.streamBurstStartDibitKnown && b.streamBurstStartDibitKnown" in main and
        "return a.streamBurstStartDibit < b.streamBurstStartDibit;" in main,
    "audio feed does not sort only by window-local dibit offset":
        "std::sort(orderedBurstsForFeed.begin(), orderedBurstsForFeed.end(), [](const P25Phase2Burst& a, const P25Phase2Burst& b) {\n        return a.dibitOffset < b.dibitOffset;" not in main,
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    print("P25 Phase 2 stream coordinate preservation regression: FAIL")
    for name in failed:
        print(f" - {name}")
    raise SystemExit(1)

normalize = decoder.split("auto normalizePhase2BurstOffsets", 1)[1].split(
    "// Keep Phase-2 call/ESS state", 1
)[0]
stamp_pos = normalize.find("cw.streamDibit = phase2WorkingStreamStart")
clamp_pos = normalize.find("cw.dibitOffset = cw.dibitOffset >= phase2PrefixDibits")
if stamp_pos < 0 or clamp_pos < 0 or stamp_pos > clamp_pos:
    print("P25 Phase 2 stream coordinate preservation regression: FAIL")
    print(" - codeword stream coordinate must be stamped before dibitOffset normalization")
    raise SystemExit(1)

print("P25 Phase 2 stream coordinate preservation regression: PASS")
