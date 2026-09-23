#!/usr/bin/env python3
from pathlib import Path

engine_path = Path("src/SatcomScannerEngine.cpp")
engine = engine_path.read_text(encoding="utf-8")

old_push = '''void SatcomScannerEngine::pushMonitorAudio(const float* samples, size_t count) {
    if (!samples || count == 0) return;
    std::lock_guard<std::mutex> lock(audioMutex_);
    if (!audio_) return;
    audio_->trimQueuedAudio(12000);
    audio_->pushAudio(samples, count);
}
'''
new_push = '''void SatcomScannerEngine::pushMonitorAudio(const float* samples, size_t count) {
    if (!samples || count == 0) return;
    std::lock_guard<std::mutex> lock(audioMutex_);
    if (!audio_) return;
    // The MainWindow engine can also be carrying Listen audio from a second SDR.
    // Trimming that shared queue would discard another receiver's PCM. Keep the
    // Satcom-only latency clamp only for the standalone fallback engine.
    if (!usingSharedAudio_.load(std::memory_order_acquire))
        audio_->trimQueuedAudio(12000);
    audio_->pushAudio(samples, count);
}
'''
if engine.count(old_push) != 1:
    raise SystemExit(f"expected one pushMonitorAudio block, found {engine.count(old_push)}")
engine = engine.replace(old_push, new_push)

old_snapshot = "        snapshot.sharedMainAudio = usingSharedAudio_;\n"
new_snapshot = "        snapshot.sharedMainAudio = usingSharedAudio_.load(std::memory_order_acquire);\n"
if engine.count(old_snapshot) != 1:
    raise SystemExit(f"expected one shared audio snapshot, found {engine.count(old_snapshot)}")
engine = engine.replace(old_snapshot, new_snapshot)
engine_path.write_text(engine, encoding="utf-8")

verify_path = Path("scripts/verify_satcom_host_integration.py")
verify = verify_path.read_text(encoding="utf-8")
anchor = '''    require("SatcomHostServices::instance().publishSpectrum" in engine_cpp,
            "engine does not publish spectrum to MainWindow")
'''
addition = '''    require("SatcomHostServices::instance().publishSpectrum" in engine_cpp,
            "engine does not publish spectrum to MainWindow")
    require("if (!usingSharedAudio_.load(std::memory_order_acquire))" in engine_cpp,
            "shared MainWindow audio queue can still be trimmed by Satcom")
'''
if verify.count(anchor) != 1:
    raise SystemExit(f"expected one verifier anchor, found {verify.count(anchor)}")
verify = verify.replace(anchor, addition)
verify_path.write_text(verify, encoding="utf-8")
