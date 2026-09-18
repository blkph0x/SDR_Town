#pragma once
#include <vector>

struct P25AudioResamplerState;
std::vector<float> resampleDecodedP25PcmWithState(P25AudioResamplerState& state,
    const std::vector<float>& pcm, double inputRate, double outputRate);
