#pragma once
#include "SstvProgress.h"
#include <memory>
#include <nlohmann/json.hpp>
struct Receiver;

// Worker-only, independent chronological cursor; never tunes hardware or writes RX state.
nlohmann::json decodeSstvRfLive(const std::shared_ptr<Receiver>& receiver,
    const QString& output,const QString& imageMode,const QString& rfMode,
    const std::function<bool()>& finish,const std::function<bool()>& cancel,
    const SstvPreview& preview,const std::function<void(const QString&)>& routeStatus);
