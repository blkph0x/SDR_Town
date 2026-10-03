#include "CwRfSession.h"
#include "DeviceManager.h"
#include "Receiver.h"
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <spdlog/spdlog.h>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
const char* modeName(DemodMode mode) {
    switch (mode) {
    case DemodMode::AM: return "AM";
    case DemodMode::USB: return "USB";
    case DemodMode::LSB: return "LSB";
    case DemodMode::CW: return "CW";
    case DemodMode::WFM: return "WFM";
    case DemodMode::NFM: return "NFM";
    default: return "Auto (NFM fallback)";
    }
}
}

CwRun cwReceiverSource(const std::shared_ptr<Receiver>& receiver) {
    require(bool(receiver), "Start the main receiver first");
    // Snapshot on the worker as well as revalidate around every IQ read. No
    // captured MainWindow pointer crosses into this thread.
    return [receiver](CwOptions options, const CwCancel& cancel, const CwPublish& publish) {
        size_t device;
        double target, bandwidth;
        DemodMode mode;
        {
            std::lock_guard lock(receiver->stateMutex);
            device = receiver->deviceIndex; target = receiver->freqHz;
            bandwidth = receiver->channelBwHz; mode = receiver->mode;
        }
        auto& manager = DeviceManager::instance();
        const auto devices = manager.getDevices();
        require(device < devices.size(), "Receiver device is unavailable");
        const auto key = devices[device].stableKey;
        const double rate = manager.getCurrentSampleRate(device), center = manager.getCurrentCenterFreq(device);
        const auto tune = manager.getCenterTuneAppliedSeq(device);
        require(std::isfinite(rate) && rate >= 8000 && rate <= 100e6, "Unsupported receiver sample rate");
        auto validate = [&] {
            {
                std::lock_guard lock(receiver->stateMutex);
                require(receiver->active && !receiver->p25VoiceDecodeEnabled && !receiver->p25ControlChannelMute,
                    "CW requires an active analog receiver, not P25");
                require(receiver->deviceIndex == device && receiver->freqHz == target &&
                    receiver->mode == mode && receiver->channelBwHz == bandwidth,
                    "Receiver changed; restart CW decoding");
            }
            const auto current = manager.getDevices();
            require(device < current.size() && current[device].stableKey == key && manager.isStreaming(device),
                "Receiver stopped or changed");
            require(manager.getRuntimeStateLabel(device) == "live hardware", "CW needs live hardware, not simulated IQ");
            require(manager.getCurrentSampleRate(device) == rate && manager.getCurrentCenterFreq(device) == center &&
                manager.getCenterTuneAppliedSeq(device) == tune && manager.getCenterTuneRequestSeq(device) == tune,
                "Device retuned; restart CW decoding");
        };
        validate();
        require(mode != DemodMode::AUTO, "Select CW, USB, LSB, AM, NFM or WFM on the main receiver before starting Morse");
        require(std::abs(target - center) + bandwidth * .5 < rate * .5, "CW receiver lies outside the device passband");
        Demodulator demod;
        Receiver cursor;
        CwDecoder decoder(options);
        manager.setReceiverCursorToLiveEdge(device, cursor);
        uint64_t iqNext = 0, iqEpoch = 0, audioNext = 0, audioEpoch = 0, gaps = 0;
        bool known = false, pendingGap = false;
        const QString source = QString("Device %1 | %2 MHz | %3").arg(device).arg(target / 1e6, 0, 'f', 6).arg(modeName(mode));
        publish({{}, source, "Receiving (experimental)", 0});
        spdlog::info("CW live start device={} mode={} rate={} bandwidth={}", device, modeName(mode), rate, bandwidth);
        const auto blockSize = size_t(std::clamp(rate / 10, 4096.0, 262144.0));
        while (!cancel()) {
            validate();
            auto window = manager.getNewIQWindowForReceiver(device, cursor, blockSize, size_t(rate * 2));
            validate();
            pendingGap = pendingGap || window.cursorDiscontinuity;
            // An idle read has default cursor bounds, not a stream restart.
            if (window.samples.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            const bool gap = !known || pendingGap || window.streamEpoch != iqEpoch || window.startAbsolute != iqNext;
            if (gap) { demod.resetState(); ++audioEpoch; audioNext = 0; if (known) ++gaps; }
            pendingGap = false;
            known = true; iqEpoch = window.streamEpoch; iqNext = window.endAbsolute;
            double level;
            // An independent audio route, deliberately pre-speaker volume/squelch.
            auto audio = demod.demodulateToAudio(window.samples, rate, center, target, mode, level,
                0, -140, 1, 75, .96, bandwidth, 0, 48000);
            decoder.process(audio, 48000, audioEpoch, audioNext, gap);
            audioNext += audio.size();
            publish({decoder.snapshot(), source, "Receiving (experimental)", gaps});
        }
        const auto stats = decoder.snapshot();
        publish({stats, source, "Stopped", gaps});
        spdlog::info("CW live stop samples={} blocks={} gaps={} resets={} processingMs={:.1f}",
            stats.samples, stats.blocks, gaps, stats.resets, stats.processingMs);
    };
}
