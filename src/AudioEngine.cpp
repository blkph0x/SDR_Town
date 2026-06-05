#define MINIAUDIO_IMPLEMENTATION
#include "AudioEngine.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>

static void data_callback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount)
{
    AudioEngine* engine = reinterpret_cast<AudioEngine*>(pDevice->pUserData);
    if (!engine) return;

    float* out = reinterpret_cast<float*>(pOutput);
    // The engine pushes data; here we just zero if nothing (or we can have a small buffer per device)
    // For the simple duplication model we use ma_device_write from the push thread instead of callback model.
    // But miniaudio requires a callback; we keep a very small silence + the engine will use write() API for push.
    std::memset(out, 0, frameCount * sizeof(float) * pDevice->playback.channels);
}

AudioEngine::AudioEngine()
{
    ma_context_config ctxCfg = ma_context_config_init();
    if (ma_context_init(nullptr, 0, &ctxCfg, &m_context) != MA_SUCCESS) {
        spdlog::error("miniaudio context init failed");
    }
    spdlog::info("AudioEngine (miniaudio) initialized");
}

AudioEngine::~AudioEngine()
{
    for (auto& act : m_active) {
        if (act.device.pContext) {
            ma_device_uninit(&act.device);
        }
    }
    ma_context_uninit(&m_context);
    spdlog::info("AudioEngine destroyed");
}

std::vector<AudioDeviceInfo> AudioEngine::enumeratePlaybackDevices()
{
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
    // stop all current
    for (size_t i = 0; i < m_active.size(); ++i) stopDevice(i);
    m_active.clear();

    for (size_t idx : indicesFromLastEnum) {
        if (idx < m_devices.size()) {
            startDevice(idx);
        }
    }
    spdlog::info("Active audio outputs set: {}", activeOutputCount());
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
    if (enumIndex >= m_devices.size()) return;

    ActiveOutput act;
    act.enumIndex = enumIndex;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = &m_devices[enumIndex].id;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = 48000;
    cfg.dataCallback = data_callback;
    cfg.pUserData = this;

    ma_result res = ma_device_init(&m_context, &cfg, &act.device);
    if (res != MA_SUCCESS) {
        spdlog::error("Failed to init playback device {}: {}", m_devices[enumIndex].name, (int)res);
        return;
    }

    res = ma_device_start(&act.device);
    if (res != MA_SUCCESS) {
        spdlog::error("Failed to start playback device {}", m_devices[enumIndex].name);
        ma_device_uninit(&act.device);
        return;
    }

    act.volume = 0.9f;
    m_active.push_back(std::move(act));
    spdlog::info("Started audio output: {}", m_devices[enumIndex].name);
}

void AudioEngine::stopDevice(size_t activeIdx)
{
    if (activeIdx >= m_active.size()) return;
    auto& act = m_active[activeIdx];
    if (act.device.pContext) {
        ma_device_stop(&act.device);
        ma_device_uninit(&act.device);
        spdlog::info("Stopped audio output #{}", activeIdx);
    }
}

void AudioEngine::setMasterVolume(float vol)
{
    m_masterVolume = std::clamp(vol, 0.0f, 1.0f);
}

void AudioEngine::setOutputVolume(size_t activeIndex, float vol)
{
    if (activeIndex < m_active.size()) {
        m_active[activeIndex].volume = std::clamp(vol, 0.0f, 1.0f);
    }
}

void AudioEngine::pushAudio(const float* samples, size_t count)
{
    if (m_active.empty()) return;

    for (auto& act : m_active) {
        float v = act.volume * m_masterVolume;
        if (v <= 0.0001f) continue;

        // For simplicity we use ma_device_write (blocking is ok for monitoring use)
        // In production a small lock-free ring per device + callback read would be lower latency.
        std::vector<float> buf(count);
        for (size_t i = 0; i < count; ++i) buf[i] = samples[i] * v;

        ma_uint32 framesWritten = 0;
        ma_device_write(&act.device, buf.data(), static_cast<ma_uint32>(count), &framesWritten);
    }

    // rudimentary test tone overlay if requested
    if (m_testTarget >= 0) {
        // handled in playTestTone by pushing a generated block instead
    }
}

void AudioEngine::playTestTone(size_t activeIndex, float freq, float durationSec)
{
    m_testFreq = freq;
    m_testTarget = (int)activeIndex;

    const int sr = 48000;
    const int total = static_cast<int>(durationSec * sr);

    std::vector<float> tone(total);
    float phase = m_testPhase.load();
    float delta = 2.0f * 3.14159265f * freq / sr;

    for (int i = 0; i < total; ++i) {
        tone[i] = 0.6f * std::sin(phase);
        phase += delta;
    }
    m_testPhase = phase;

    if (activeIndex == size_t(-1)) {
        // all
        for (size_t i = 0; i < m_active.size(); ++i) {
            float v = m_active[i].volume * m_masterVolume;
            std::vector<float> scaled = tone;
            for (auto& s : scaled) s *= v;
            ma_uint32 written = 0;
            ma_device_write(&m_active[i].device, scaled.data(), static_cast<ma_uint32>(scaled.size()), &written);
        }
    } else if (activeIndex < m_active.size()) {
        float v = m_active[activeIndex].volume * m_masterVolume;
        for (auto& s : tone) s *= v;
        ma_uint32 written = 0;
        ma_device_write(&m_active[activeIndex].device, tone.data(), static_cast<ma_uint32>(tone.size()), &written);
    }

    m_testTarget = -1; // one-shot
}

size_t AudioEngine::activeOutputCount() const { return m_active.size(); }

bool AudioEngine::isDeviceActive(size_t enumIndex) const
{
    for (const auto& a : m_active) if (a.enumIndex == enumIndex) return true;
    return false;
}

std::string AudioEngine::getActiveDeviceNames() const
{
    std::string s;
    for (size_t i = 0; i < m_active.size(); ++i) {
        if (i > 0) s += " | ";
        s += m_devices[m_active[i].enumIndex].name;
    }
    return s.empty() ? "None" : s;
}