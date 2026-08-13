#include "audio/audio_graph.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("passthrough copies stereo samples exactly")
{
    namfx::audio::AudioGraph graph;

    constexpr int n = 1024;
    std::vector<float> inL(static_cast<std::size_t>(n));
    std::vector<float> inR(static_cast<std::size_t>(n));
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i) {
        inL[static_cast<std::size_t>(i)] = static_cast<float>(i - 512) / 512.0f;
        inR[static_cast<std::size_t>(i)] = static_cast<float>(512 - i) / 512.0f;
    }

    graph.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);

    for (int i = 0; i < n; ++i) {
        INFO("sample " << i);
        REQUIRE(static_cast<double>(outL[static_cast<std::size_t>(i)])
                == static_cast<double>(inL[static_cast<std::size_t>(i)]));
        REQUIRE(static_cast<double>(outR[static_cast<std::size_t>(i)])
                == static_cast<double>(inR[static_cast<std::size_t>(i)]));
    }
}
