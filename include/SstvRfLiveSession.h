#pragma once
#include "SstvProgress.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
struct Receiver;

// 48 kHz mono from the SSTV demod. The window plays this when the main
// speaker path is not already carrying the same listen.
using SstvAudioMonitor = std::function<void(const float* samples, std::size_t count, double sampleRateHz)>;

// Worker-only, independent chronological cursor; never tunes hardware or writes RX state.
nlohmann::json decodeSstvRfLive(const std::shared_ptr<Receiver>& receiver,
    const QString& output,const QString& imageMode,const QString& rfMode,
    const std::function<bool()>& finish,const std::function<bool()>& cancel,
    const SstvPreview& preview,const std::function<void(const QString&)>& routeStatus,
    const SstvAudioMonitor& monitor = {});

nlohmann::json decodeSstvDedicatedRadio(const QString& deviceKey, double frequencyHz,
    const QString& output, const QString& imageMode, const QString& rfMode,
    const std::function<bool()>& finish, const std::function<bool()>& cancel,
    const SstvPreview& preview, const std::function<void(const QString&)>& routeStatus,
    const SstvAudioMonitor& monitor = {});
