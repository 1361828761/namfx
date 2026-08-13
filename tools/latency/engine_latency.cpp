#include "audio/audio_graph.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

int main()
{
    constexpr int kSamples = 64;
    constexpr std::size_t kWarmup = 100;
    constexpr std::size_t kIterations = 1000;
    constexpr long long kThresholdNs = 500000;

    std::vector<float> inL(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> inR(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> outL(static_cast<std::size_t>(kSamples), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kSamples), 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    namfx::audio::AudioGraph graph;

    for (std::size_t i = 0; i < kWarmup; ++i) {
        graph.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kSamples);
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        graph.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kSamples);
    }
    const auto t1 = std::chrono::steady_clock::now();

    const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        / static_cast<long long>(kIterations);

    std::printf("engine latency: %lld ns\n", ns);

    for (int i = 0; i < kSamples; ++i) {
        if (outL[i] != inL[i] || outR[i] != inR[i]) {
            std::printf("ERROR: passthrough mismatch at sample %d\n", i);
            return 1;
        }
    }

    return ns < kThresholdNs ? 0 : 1;
}
