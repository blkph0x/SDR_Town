#pragma once
#include "RdsMpxDecoder.h"
#include <functional>
#include <string>
// Decode a mono MPX recording, not 48 kHz speaker audio. Callback per input chunk.
RdsMpxSnapshot decodeRdsMpxFile(const std::string& path, size_t chunkSize=8192,
    const std::function<void(const RdsMpxSnapshot&)>& update={});
