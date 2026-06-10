#define MINIAUDIO_IMPLEMENTATION
#include "AudioEngine.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>

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
            size_t available = (wpos - rpos) & (rb.capacity - 1);

            size_t canRead = std::min(toRead, available);

            for (size_t i = 0; i < canRead; ++i) {
                out[i] = rb.data[(rpos + i) & (rb.capacity - 1)] * v;
            }
            read += canRead;
            rpos = (rpos + canRead) & (rb.capacity - 1);
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
    for (auto& act : m_active) {
        if (act && act->device && act->device->pContext) {
            ma_device_uninit(act->device.get());
        }
    }
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
    std::lock_guard<std::mutex> lk(audioMutex);
    // stop all current (ma_stop first to quiesce callbacks, then clear vector of shared_ptrs)
    for (size_t i = 0; i < m_active.size(); ++i) stopDevice(i);
    m_active.clear();

    for (size_t idx : indicesFromLastEnum) {
        if (idx < m_devices.size()) {
            startDevice(idx);
        }
    }
    spdlog::info("Active audio outputs set: {}", m_active.size());
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

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = &m_devices[enumIndex].id;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = 48000;
    cfg.dataCallback = data_callback;
    cfg.pUserData = this;

    auto dev = std::make_unique<ma_device>();
    ma_result res = ma_device_init(&m_context, &cfg, dev.get());
    if (res != MA_SUCCESS) {
        spdlog::error("Failed to init playback device {}: {}", m_devices[enumIndex].name, (int)res);
        return;
    }

    float actualRate = (float)cfg.sampleRate;
    m_sampleRate.store(actualRate, std::memory_order_relaxed);

    // Add to m_active and init ring *before* starting the device.
    auto actPtr = std::make_shared<ActiveOutput>();
    actPtr->enumIndex = enumIndex;
    actPtr->device = std::move(dev);
    actPtr->volume = 0.9f;
    actPtr->owningEngine = this;

    // P1: give *this device's* ma_device a direct pointer to its ActiveOutput so data_callback has zero-cost access
    // (no m_active iteration or findActiveOutput from the realtime thread). The engine is reachable via owningEngine.
    actPtr->device.get()->pUserData = actPtr.get();

    actPtr->ring.init(48000 * 4); // ~4 seconds of audio, power-of-2
    actPtr->valid.store(true, std::memory_order_release);

    m_active.push_back(actPtr);

    res = ma_device_start(actPtr->device.get());
    if (res != MA_SUCCESS) {
        spdlog::error("Failed to start playback device {}", m_devices[enumIndex].name);
        actPtr->valid.store(false);
        // remove the bad entry
        m_active.pop_back();
        return;
    }

    spdlog::info("Started audio output: {} @ {} Hz ({} bit float, mono)", m_devices[enumIndex].name, (int)actualRate, 32);
    spdlog::info("  All audio block sizes, de-emphasis, 19kHz notch, final LPF are calculated for this exact device rate.", (int)actualRate);
}

void AudioEngine::stopDevice(size_t activeIdx)
{
    if (activeIdx >= m_active.size()) return;
    auto& actPtr = m_active[activeIdx];
    if (actPtr) {
        actPtr->valid.store(false, std::memory_order_release);
        if (actPtr->device && actPtr->device->pContext) {
            ma_device_stop(actPtr->device.get());
            ma_device_uninit(actPtr->device.get());
            spdlog::info("Stopped audio output #{}", activeIdx);
        }
    }
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
    audioBuffer.clear();
}

void AudioEngine::pushAudio(const float* samples, size_t count)
{
    if (!samples || count == 0) return;
    std::lock_guard<std::mutex> lk(audioMutex);

    for (auto& actPtr : m_active) {
        if (!actPtr || !actPtr->valid.load(std::memory_order_acquire)) continue;
        auto& rb = actPtr->ring;
        if (rb.capacity == 0) continue;

        const size_t maxQueuedFrames = std::max<size_t>(256, (size_t)(getSampleRate() * 0.25f));
        size_t w = rb.writePos.load(std::memory_order_relaxed);
        size_t r = rb.readPos.load(std::memory_order_acquire);
        size_t queued = (w - r) & (rb.capacity - 1);
        if (queued + count > maxQueuedFrames) {
            const size_t keep = (maxQueuedFrames > count) ? (maxQueuedFrames - count) : 0;
            const size_t newRead = (w + rb.capacity - keep) & (rb.capacity - 1);
            rb.readPos.store(newRead, std::memory_order_release);
        }

        for (size_t i = 0; i < count; ++i) {
            size_t next = (w + 1) & (rb.capacity - 1);
            if (next == rb.readPos.load(std::memory_order_acquire)) {
                // ring full - advance read to drop oldest
                rb.readPos.store((rb.readPos.load(std::memory_order_relaxed) + 1) & (rb.capacity - 1), std::memory_order_relaxed);
            }
            rb.data[w] = samples[i];
            w = next;
        }
        rb.writePos.store(w, std::memory_order_release);
    }

    // legacy shared buffer kept for compatibility / tests
    audioBuffer.insert(audioBuffer.end(), samples, samples + count);
    size_t bound = (size_t)(getSampleRate() * 2);
    if (audioBuffer.size() > bound) {
        audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + (audioBuffer.size() - bound));
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

double AudioEngine::getRingFillPercent() const {
    std::lock_guard<std::mutex> lk(audioMutex);
    // Approx for first valid active (used in live stats). 0 if none.
    for (auto& act : m_active) {
        if (act && act->valid.load(std::memory_order_acquire)) {
            auto& rb = act->ring;
            size_t r = rb.readPos.load(std::memory_order_relaxed);
            size_t w = rb.writePos.load(std::memory_order_acquire);
            size_t cap = (rb.capacity ? rb.capacity : 1);
            size_t avail = (w - r) & (cap - 1);
            return (100.0 * avail) / cap;
        }
    }
    return 0.0;
}

int AudioEngine::getUnderrunCount() const {
    return underrunCount.load(std::memory_order_relaxed);
}
