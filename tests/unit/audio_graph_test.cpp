#include "audio/audio_graph.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("passthrough copies samples exactly")
{
    namfx::audio::AudioGraph graph;

    const std::size_t n = 1024;
    std::vector<float> in(n);
    std::vector<float> out(n);

    for (std::size_t i = 0; i < n; ++i) {
        in[i] = static_cast<float>(static_cast<int>(i) - 512) / 512.0f;
    }

    graph.processBlock(in.data(), out.data(), n);

    for (std::size_t i = 0; i < n; ++i) {
        INFO("sample " << i);
        REQUIRE(static_cast<double>(out[i]) == static_cast<double>(in[i]));
    }
}

TEST_CASE("commit and swap flip the live buffer")
{
    namfx::audio::AudioGraph graph;

    REQUIRE(graph.front() == 0);

    graph.swap();
    REQUIRE(graph.front() == 0);

    graph.commit();
    graph.swap();
    REQUIRE(graph.front() == 1);

    graph.commit();
    graph.swap();
    REQUIRE(graph.front() == 0);
}
