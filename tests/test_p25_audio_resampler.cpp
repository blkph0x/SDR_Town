#include "P25AudioResampler.h"
#include "P25ReceiverSession.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>

TEST_CASE("P25 PCM interpolation is independent of producer block boundaries", "[p25][resampler]") {
    std::vector<float> pcm(8000);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<float>(0.3 * std::sin(2 * 3.141592653589793 * 937 * i / 8000));
    for (double outputRate : {48000.0, 44100.0}) {
        P25AudioResamplerState wholeState;
        const auto whole = resampleDecodedP25PcmWithState(wholeState, pcm, 8000, outputRate);
        REQUIRE(whole.size() == static_cast<size_t>(outputRate));
        for (size_t chunk : {160u, 480u, 37u, 1u}) {
            P25AudioResamplerState state;
            std::vector<float> pieces;
            for (size_t start = 0; start < pcm.size(); start += chunk) {
                const size_t end = std::min(pcm.size(), start + chunk);
                const std::vector<float> part(pcm.begin() + start, pcm.begin() + end);
                const auto result = resampleDecodedP25PcmWithState(state, part, 8000, outputRate);
                pieces.insert(pieces.end(), result.begin(), result.end());
            }
            INFO("rate=" << outputRate << " chunk=" << chunk);
            REQUIRE(pieces.size() == whole.size());
            double maxError = 0;
            for (size_t i = 0; i < whole.size(); ++i)
                maxError = std::max(maxError, std::abs(static_cast<double>(whole[i] - pieces[i])));
            REQUIRE(maxError < 0.00001);
        }
    }
}
