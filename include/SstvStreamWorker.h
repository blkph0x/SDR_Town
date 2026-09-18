#pragma once
#include "SstvLiveInput.h"
#include "SstvProgress.h"
#include <nlohmann/json.hpp>
#include <variant>

struct SstvStreamIdle {};
struct SstvStreamEnd {};
using SstvStreamItem=std::variant<SstvInputEvent,SstvStreamIdle,SstvStreamEnd>;
using SstvStreamRead=std::function<SstvStreamItem()>;
struct SstvStreamResult {
    std::vector<QImage> images;
    nlohmann::json metadata;
    uint64_t inputSamples=0,outputSamples=0,sourceId=0,epoch=0,generation=0;
    double inputRate=0,targetHz=0;
};

// DEC-0098: synchronous on one non-GUI owner thread. next() must be nonblocking.
// Preview is provisional and worker-thread-only; discard it if this throws.
SstvStreamResult decodeSstvStream(const SstvStreamRead& next,const QString& mode,
                                 const std::function<bool()>& cancelled={},
                                 const SstvPreview& preview={});
