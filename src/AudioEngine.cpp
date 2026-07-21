#define MINIAUDIO_IMPLEMENTATION
#include "AudioEngine.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <memory>

namespace {
constexpr size_t kOutputRingFrames = 1u << 19; // storage capacity; queued depth is capped below for jitter buffering.
// Field 20260720_070533: 220 ms silence-bridge floor was still drained by
// selected-slot emit droughts (p90 gap ~160 ms, outliers 0.5–3.6 s). Keep a
// deeper digital-voice jitter cap so pending→push can pre-buffer real AMBE
// without inventing opposite-slot PLC.
constexpr double kDigitalVoiceJitterSeconds = 0.85;
static_assert((kOutputRingFrames & (kOutputRingFrames - 1)) == 0,
              "Audio output ring capacity must stay a power of two for the fast wrap path.");

static bool isPowerOfTwo(size_t v) noexcept
{
    return v != 0 && (v & (v - 1)) == 0;
}

static size_t ringWrap(size_t value, size_t capacity) noexcept
{
    if (capacity == 0) return 0;
    return isPowerOfTwo(capacity) ? (value & (capacity - 1)) : (value % capacity);
}

static size_t ringDistance(size_t writePos, size_t readPos, size_t capacity) noexcept
{
    if (capacity == 0) return 0;
    if (isPowerOfTwo(capacity)) return (writePos - readPos) & (capacity - 1);
    return writePos >= readPos ? (writePos - readPos) : (capacity - (readPos - writePos));
}

static void stopAndUninitOutput(const std::shared_ptr<AudioEngine::ActiveOutput>& act)
{
    if (!act) return;
    act->valid.store(false, std::memory_order_release);
    if (act->device && act->device->pContext) {
        ma_device_stop(act->device.get());
        ma_device_uninit(act->device.get());
        act->device.reset();
    }
}
}

static void data_callback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount)
{
    // P1: direct pointer from this device's pUserData (set at startDevice time) avoids any m_active walk or find from RT thread.
    auto* myAct = reinterpret_cast<AudioEngine::ActiveOutput*>(pDevice->pUserData);
    if (!myAct) {
        std::memset(pOutput, 0, frameCount * sizeof(float));
        return;
    }
    AudioEngine* engine = myAct->owningEngine;
    if (!engine) {
        std::memset(pOutput, 0, frameCount * sizeof(float));
        return;
    }

    float* out = reinterpret_cast<float*>(pOutput);

    if (myAct && myAct->isTestTone.load()) {
        // Per-output test tone using countdown (no detached thread).
        float freq = myAct->testFreq.load();
        float phase = myAct->testPhase.load();
        float delta = 2.0f * 3.14159265f * freq / engine->getSampleRate();

        for (ma_uint32 i = 0; i < frameCount; ++i) {
            out[i] = 0.6f * std::sin(phase);
            phase += delta;
            if (phase > 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
        }
        myAct->testPhase.store(phase);

        int remaining = myAct->testToneFramesLeft.load();
        if (remaining > 0) {
            remaining -= (int)frameCount;
            if (remaining <= 0) {
                myAct->isTestTone.store(false);
                myAct->testToneFramesLeft.store(0);
            } else {
                myAct->testToneFramesLeft.store(remaining);
            }
        }
    } else {
        // Real audio path - lock-free ring buffer read (no mutex, no erase, no alloc in RT callback).
        // This prevents the crackles/stalls caused by locking + vector erase from the realtime thread.
        // Defensive check for races during setActiveOutputs (m_active clear while callback runs).
        if (myAct && myAct->valid.load(std::memory_order_acquire)) {
            auto& rb = myAct->ring;
            float master = engine->getMasterVolume();
            float v = master * myAct->volume.load();
            size_t toRead = frameCount;
            size_t read = 0;

            size_t rpos = rb.readPos.load(std::memory_order_relaxed);
            size_t wpos = rb.writePos.load(std::memory_order_acquire);
            if (rb.capacity == 0) {
                std::memset(out, 0, toRead * sizeof(float));
                return;
            }
            size_t available = ringDistance(wpos, rpos, rb.capacity);

            size_t canRead = std::min(toRead, available);

            for (size_t i = 0; i < canRead; ++i) {
                out[i] = rb.data[ringWrap(rpos + i, rb.capacity)] * v;
            }
            read += canRead;
            rpos = ringWrap(rpos + canRead, rb.capacity);
            rb.readPos.store(rpos, std::memory_order_release);

            if (read < toRead) {
                // underrun - zero the rest (better than garbage).
                // P2: do NOT log from the realtime audio callback (can allocate/block and worsen underruns).
                // Just bump the atomic; UI/CLI stats will report the current count on demand or timer.
                ++engine->underrunCount;
                std::memset(out + read, 0, (toRead - read) * sizeof(float));
            }
        } else {
            std::memset(out, 0, frameCount * sizeof(float));
        }
    }
}

AudioEngine::AudioEngine()
{
    ma_context_config ctxCfg = ma_context_config_init();
    if (ma_context_init(nullptr, 0, &ctxCfg, &m_context) != MA_SUCCESS) {
        spdlog::error("miniaudio context init failed");
        m_contextValid = false;
    } else {
        m_contextValid = true;
        spdlog::info("AudioEngine (miniaudio) initialized");
    }
}

AudioEngine::~AudioEngine()
{
    std::vector<std::shared_ptr<ActiveOutput>> oldOutputs;
    {
        std::lock_guard<std::mutex> lk(audioMutex);
        for (auto& act : m_active) {
            if (act) act->valid.store(false, std::memory_order_release);
        }
        oldOutputs.swap(m_active);
    }

    // Stop/uninit outside audioMutex. This lets miniaudio join any in-flight callback
    // scopes while ActiveOutput storage remains alive in oldOutputs, avoiding UAF.
    for (auto& act : oldOutputs) stopAndUninitOutput(act);
    oldOutputs.clear();

    if (m_contextValid) {
        ma_context_uninit(&m_context);
    }
    spdlog::info("AudioEngine destroyed");
}

std::vector<AudioDeviceInfo> AudioEngine::enumeratePlaybackDevices()
{
    if (!m_contextValid) {
        spdlog::warn("Audio context not valid, cannot enumerate playback devices");
        return {};
    }
    m_devices.clear();

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_result res = ma_context_get_devices(&m_context, &pPlaybackInfos, &playbackCount, nullptr, nullptr);
    if (res != MA_SUCCESS) {
        spdlog::warn("ma_context_get_devices failed");
        return {};
    }

    ma_device_info defaultInfo{};
    ma_context_get_device_info(&m_context, ma_device_type_playback, nullptr, &defaultInfo);

    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        AudioDeviceInfo info;
        info.id = pPlaybackInfos[i].id;
        info.name = pPlaybackInfos[i].name;
        info.isDefault = (std::memcmp(&pPlaybackInfos[i].id, &defaultInfo.id, sizeof(ma_device_id)) == 0);
        m_devices.push_back(info);
    }

    spdlog::info("Enumerated {} playback devices", m_devices.size());
    for (const auto& d : m_devices) {
        spdlog::debug("  - {} {}", d.name, d.isDefault ? "(default)" : "");
    }
    return m_devices;
}

void AudioEngine::setActiveOutputs(const std::vector<size_t>& indicesFromLastEnum)
{
    if (!m_contextValid) {
        spdlog::warn("Audio context not valid, cannot set active outputs");
        return;
    }

    std::vector<std::shared_ptr<ActiveOutput>> oldOutputs;
    {
        std::lock_guard<std::mutex> lk(audioMutex);
        for (auto& act : m_active) {
            if (act) act->valid.store(false, std::memory_order_release);
        }
        oldOutputs.swap(m_active);
    }

    for (auto& act : oldOutputs) stopAndUninitOutput(act);
    oldOutputs.clear();

    {
        std::lock_guard<std::mutex> lk(audioMutex);
        for (size_t idx : indicesFromLastEnum) {
            if (idx < m_devices.size()) {
                startDevice(idx);
            }
        }
        spdlog::info("Active audio outputs set: {}", m_active.size());
    }
}

void AudioEngine::setActiveOutputsByName(const std::vector<std::string>& nameSubstrings)
{
    std::vector<size_t> indices;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        for (const auto& sub : nameSubstrings) {
            if (m_devices[i].name.find(sub) != std::string::npos) {
                indices.push_back(i);
                break;
            }
        }
    }
    setActiveOutputs(indices);
}

void AudioEngine::startDevice(size_t enumIndex)
{
    if (!m_contextValid || enumIndex >= m_devices.size()) return;

    auto actPtr = std::make_shared<ActiveOutput>();
    actPtr->enumIndex = enumIndex;
    actPtr->volume = 0.9f;
    actPtr->owningEngine = this;
    actPtr->ring.init(kOutputRingFrames);
    if (!isPowerOfTwo(actPtr->ring.capacity)) {
        spdlog::error("Audio ring capacity {} is not a power of two; refusing to start output {}.",
                      actPtr->ring.capacity, m_devices[enumIndex].name);
        return;
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = &m_devices[enumIndex].id;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = 48000;
    cfg.dataCallback = data_callback;
    // The realtime callback reinterpret_casts pUserData as ActiveOutput, so pass
    // the exact ActiveOutput object from the outset rather than the AudioEngine.
    cfg.pUserData = actPtr.get();

    auto dev = std::make_unique<ma_device>();
    ma_result res = ma_device_init(&m_context, &cfg, dev.get());
    if (res != MA_SUCCESS) {
        spdlog::error("Failed to init playback device {}: {}", m_devices[enumIndex].name, (int)res);
        return;
    }

    actPtr->device = std::move(dev);
    const ma_uint32 actualDeviceRate = actPtr->device ? actPtr->device->sampleRate : cfg.sampleRate;
    float actualRate = static_cast<float>(actualDeviceRate != 0 ? actualDeviceRate : cfg.sampleRate);
    m_sampleRate.store(actualRate, std::memory_order_relaxed);
    actPtr->device->pUserData = actPtr.get();
    actPtr->valid.store(true, std::memory_order_release);

    m_active.push_back(actPtr);

    res = ma_device_start(actPtr->device.get());
    if (res != MA_SUCCESS) {
        spdlog::error("Failed to start playback device {}", m_devices[enumIndex].name);
        actPtr->valid.store(false, std::memory_order_release);
        if (actPtr->device && actPtr->device->pContext) {
            ma_device_uninit(actPtr->device.get());
        }
        m_active.pop_back();
        return;
    }

    spdlog::info("Started audio output: {} @ {} Hz ({} bit float, mono)", m_devices[enumIndex].name, (int)actualRate, 32);
    spdlog::info("  All audio block sizes, de-emphasis, 19kHz notch, final LPF are calculated for {} Hz.", (int)actualRate);
}

void AudioEngine::stopDevice(size_t activeIdx)
{
    std::shared_ptr<ActiveOutput> actPtr;
    {
        std::lock_guard<std::mutex> lk(audioMutex);
        if (activeIdx >= m_active.size()) return;
        actPtr = m_active[activeIdx];
        m_active.erase(m_active.begin() + static_cast<std::ptrdiff_t>(activeIdx));
    }

    // Stop/uninit outside audioMutex so a backend teardown cannot deadlock with
    // a callback or another control path that only needs the active-output list.
    stopAndUninitOutput(actPtr);
    spdlog::info("Stopped audio output #{}", activeIdx);
}

void AudioEngine::setMasterVolume(float vol)
{
    m_masterVolume.store(std::clamp(vol, 0.0f, 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setOutputVolume(size_t activeIndex, float vol)
{
    std::lock_guard<std::mutex> lk(audioMutex);
    if (activeIndex < m_active.size() && m_active[activeIndex]) {
        m_active[activeIndex]->volume = std::clamp(vol, 0.0f, 1.0f);
    }
}

std::shared_ptr<AudioEngine::ActiveOutput> AudioEngine::findActiveOutput(ma_device* pDev)
{
    for (auto& a : m_active) {
        if (a && a->device && a->device.get() == pDev) return a;
    }
    return nullptr;
}

void AudioEngine::clearBuffers()
{
    std::lock_guard<std::mutex> lk(audioMutex);
    for (auto& actPtr : m_active) {
        if (!actPtr || !actPtr->valid.load(std::memory_order_acquire)) continue;
        auto& rb = actPtr->ring;
        if (rb.capacity == 0) continue;
        const size_t w = rb.writePos.load(std::memory_order_acquire);
        rb.readPos.store(w, std::memory_order_release);
    }
}

void AudioEngine::pushAudioToActiveOutputLocked(ActiveOutput& output, const float* samples, size_t count)
{
    if (!output.valid.load(std::memory_order_acquire)) return;
    auto& rb = output.ring;
    if (rb.capacity == 0) return;

    // Digital voice decoders synthesize fixed 20 ms PCM frames.  We need enough
    // depth for one Phase-2 superframe-sized producer burst, but not a 1.5 second
    // backlog.  Keep the SPSC ownership strict: the producer must not advance
    // rb.readPos while the realtime callback is consuming audio.  On overload,
    // drop from the incoming producer block and let the callback drain naturally.
    const size_t maxQueuedFrames = getJitterQueueCapFrames();
    size_t w = rb.writePos.load(std::memory_order_relaxed);
    size_t r = rb.readPos.load(std::memory_order_acquire);
    size_t queued = ringDistance(w, r, rb.capacity);

    if (queued >= maxQueuedFrames) return;

    const size_t allowedByDepth = (queued < maxQueuedFrames) ? (maxQueuedFrames - queued) : 0;
    if (count > allowedByDepth) {
        // Drop the oldest part of an unusually large producer block, keeping the
        // most recent speech rather than adding a late burst to the queue.
        const size_t drop = count - allowedByDepth;
        samples += drop;
        count = allowedByDepth;
        if (count == 0) return;
    }

    for (size_t i = 0; i < count; ++i) {
        const size_t next = ringWrap(w + 1, rb.capacity);
        if (next == rb.readPos.load(std::memory_order_acquire)) break;
        rb.data[w] = samples[i];
        w = next;
    }
    rb.writePos.store(w, std::memory_order_release);
}

void AudioEngine::pushAudio(const float* samples, size_t count)
{
    if (!samples || count == 0) return;
    std::lock_guard<std::mutex> lk(audioMutex);

    for (auto& actPtr : m_active) {
        if (actPtr) pushAudioToActiveOutputLocked(*actPtr, samples, count);
    }
}

void AudioEngine::pushAudioToActiveOutputs(const float* samples, size_t count, const std::vector<size_t>& activeOutputIndices)
{
    if (!samples || count == 0) return;
    if (activeOutputIndices.empty()) {
        pushAudio(samples, count);
        return;
    }

    std::lock_guard<std::mutex> lk(audioMutex);
    std::vector<size_t> pushed;
    pushed.reserve(activeOutputIndices.size());
    for (size_t activeIndex : activeOutputIndices) {
        if (activeIndex >= m_active.size() || !m_active[activeIndex]) continue;
        if (std::find(pushed.begin(), pushed.end(), activeIndex) != pushed.end()) continue;
        pushAudioToActiveOutputLocked(*m_active[activeIndex], samples, count);
        pushed.push_back(activeIndex);
    }
}

void AudioEngine::playTestTone(size_t activeIndex, float freq, float durationSec)
{
    std::lock_guard<std::mutex> lk(audioMutex);

    auto setTone = [&](ActiveOutput& act) {
        act.isTestTone.store(true);
        act.testFreq.store(freq);
        act.testPhase.store(0.0f);
        // Use sample-accurate countdown in the realtime callback instead of a
        // detached sleep thread that could outlive *this and cause UAF on audioMutex / m_active.
        int frames = (int)(durationSec * getSampleRate() + 0.5f);
        act.testToneFramesLeft.store(frames > 0 ? frames : 1);
    };

    if (activeIndex == size_t(-1)) {
        for (auto& actPtr : m_active) if (actPtr) setTone(*actPtr);
        spdlog::info("Test tone requested on ALL outputs @ {} Hz for {}s", freq, durationSec);
    } else if (activeIndex < m_active.size() && m_active[activeIndex]) {
        setTone(*m_active[activeIndex]);
        spdlog::info("Test tone requested on output {} @ {} Hz for {}s", activeIndex, freq, durationSec);
    } else {
        return;
    }
}

void AudioEngine::playTestToneForDevice(size_t enumIndex, float freq, float durationSec)
{
    size_t activeIndex = size_t(-1);
    {
        std::lock_guard<std::mutex> lk(audioMutex);
        for (size_t i = 0; i < m_active.size(); ++i) {
            if (m_active[i] && m_active[i]->enumIndex == enumIndex) {
                activeIndex = i;
                break;
            }
        }
    }
    if (activeIndex != size_t(-1)) {
        playTestTone(activeIndex, freq, durationSec);
    }
}

size_t AudioEngine::activeOutputCount() const {
    std::lock_guard<std::mutex> lk(audioMutex);
    return m_active.size();
}

bool AudioEngine::isDeviceActive(size_t enumIndex) const
{
    std::lock_guard<std::mutex> lk(audioMutex);
    for (const auto& aPtr : m_active) if (aPtr && aPtr->enumIndex == enumIndex) return true;
    return false;
}

std::string AudioEngine::getActiveDeviceNames() const
{
    std::lock_guard<std::mutex> lk(audioMutex);
    std::string s;
    for (size_t i = 0; i < m_active.size(); ++i) {
        if (m_active[i] && m_active[i]->enumIndex < m_devices.size()) {
            if (i > 0) s += " | ";
            s += m_devices[m_active[i]->enumIndex].name;
        }
    }
    return s.empty() ? "None" : s;
}

size_t AudioEngine::getJitterQueueCapFrames() const {
    const size_t sampleRate = std::max<size_t>(8000, (size_t)getSampleRate());
    return std::max<size_t>(4096, (size_t)(sampleRate * kDigitalVoiceJitterSeconds));
}

size_t AudioEngine::getRingQueuedSamples() const {
    std::lock_guard<std::mutex> lk(audioMutex);
    for (auto& act : m_active) {
        if (act && act->valid.load(std::memory_order_acquire)) {
            auto& rb = act->ring;
            const size_t r = rb.readPos.load(std::memory_order_relaxed);
            const size_t w = rb.writePos.load(std::memory_order_acquire);
            const size_t cap = (rb.capacity ? rb.capacity : 1);
            return ringDistance(w, r, cap);
        }
    }
    return 0;
}

double AudioEngine::getRingFillPercent() const {
    std::lock_guard<std::mutex> lk(audioMutex);
    const size_t jitterCapFrames = getJitterQueueCapFrames();
    // Report fill relative to the live jitter queue cap, not the huge SPSC ring
    // capacity.  A few 20 ms PCM frames looked like 0.3% on the old metric even
    // when the jitter queue was reasonably fed.
    for (auto& act : m_active) {
        if (act && act->valid.load(std::memory_order_acquire)) {
            auto& rb = act->ring;
            size_t r = rb.readPos.load(std::memory_order_relaxed);
            size_t w = rb.writePos.load(std::memory_order_acquire);
            size_t cap = (rb.capacity ? rb.capacity : 1);
            size_t avail = ringDistance(w, r, cap);
            const size_t denom = std::max<size_t>(1, std::min(jitterCapFrames, cap));
            return (100.0 * avail) / denom;
        }
    }
    return 0.0;
}

int AudioEngine::getUnderrunCount() const {
    return underrunCount.load(std::memory_order_relaxed);
}
