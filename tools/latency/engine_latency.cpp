#include "audio/audio_graph.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

int main()
{
    constexpr std::size_t kSamples = 64;
    constexpr std::size_t kWarmup = 100;
    constexpr std::size_t kIterations = 1000;
    constexpr long long kThresholdNs = 500000;

    std::vector<float> in(kSamples, 0.0f);
    std::vector<float> out(kSamples, 0.0f);
    in[0] = 1.0f;

    namfx::audio::AudioGraph graph;

    for (std::size_t i = 0; i < kWarmup; ++i) {
        graph.processBlock(in.data(), out.data(), kSamples);
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        graph.processBlock(in.data(), out.data(), kSamples);
    }
    const auto t1 = std::chrono::steady_clock::now();

    const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
        / static_cast<long long>(kIterations);

    std::printf("engine latency: %lld ns\n", ns);

    if (out[0] != in[0]) {
        std::printf("ERROR: passthrough mismatch\n");
        return 1;
    }
    for (std::size_t i = 1; i < kSamples; ++i) {
        if (out[i] != in[i]) {
            std::printf("ERROR: passthrough mismatch at sample %zu\n", i);
            return 1;
        }
    }

    return ns < kThresholdNs ? 0 : 1;
}
