#pragma once
#include "SstvReceiverFeed.h"
#include "SstvProgress.h"
#include <nlohmann/json.hpp>

// Worker-only; receiver validation must not access GUI widgets.
nlohmann::json decodeSstvLive(const std::shared_ptr<SstvReceiverFeed>& feed,
    const std::function<void()>& validateReceiver,const QString& output,const QString& mode,
    const std::function<bool()>& finish,const std::function<bool()>& cancel,const SstvPreview& preview);
