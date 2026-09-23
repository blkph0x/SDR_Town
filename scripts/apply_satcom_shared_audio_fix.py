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

if old_push in engine:
    engine = engine.replace(old_push, new_push, 1)
elif new_push not in engine:
    raise SystemExit("pushMonitorAudio no longer matches either expected implementation")

old_snapshot = "        snapshot.sharedMainAudio = usingSharedAudio_;\n"
new_snapshot = "        snapshot.sharedMainAudio = usingSharedAudio_.load(std::memory_order_acquire);\n"
if old_snapshot in engine:
    engine = engine.replace(old_snapshot, new_snapshot, 1)
elif new_snapshot not in engine:
    raise SystemExit("sharedMainAudio snapshot no longer matches either expected implementation")

engine_path.write_text(engine, encoding="utf-8")
