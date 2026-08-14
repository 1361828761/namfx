#include "audio/output_stage.h"
#include "platform/rt_alloc.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using namfx::audio::OutputStage;

std::vector<float> sine(std::size_t n, double freq, double rate, float amp = 0.2f)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = amp * static_cast<float>(
                         std::sin(6.28318530717958647692 * freq * static_cast<double>(i) / rate));
    }
    return x;
}

float peak(const std::vector<float>& x, std::size_t skip = 0)
{
    float p = 0.0f;
    for (std::size_t i = skip; i < x.size(); ++i) {
        p = std::max(p, std::fabs(x[i]));
    }
    return p;
}

} // namespace

TEST_CASE("output stage applies input gain, master volume and mute")
{
    const std::vector<float> in = sine(48000, 440.0, 48000.0, 0.2f);

    OutputStage gainStage;
    gainStage.prepare(48000.0, 64);
    std::vector<float> out(in.size(), 0.0f);
    std::vector<float> outR(in.size(), 0.0f);
    gainStage.setInputGain(6.0f); // +6 dB
    gainStage.process(in.data(), in.data(), out.data(), outR.data(), static_cast<int>(in.size()));
    REQUIRE(std::fabs(peak(out, 4800) - 0.2f * 1.995f) < 0.01f);

    OutputStage masterStage;
    masterStage.prepare(48000.0, 64);
    masterStage.setMasterVolume(-12.0f);
    masterStage.process(in.data(), in.data(), out.data(), outR.data(),
                        static_cast<int>(in.size()));
    masterStage.process(in.data(), in.data(), out.data(), outR.data(),
                        static_cast<int>(in.size()));
    REQUIRE(std::fabs(peak(out, 4800) - 0.2f * 0.2512f) < 0.005f);

    OutputStage muteStage;
    muteStage.prepare(48000.0, 64);
    muteStage.setMute(true);
    // the mute is smoothed on the audio thread (fade over a few ms, no pop);
    // give the ramp time to settle, then the tail must be silent
    muteStage.process(in.data(), in.data(), out.data(), outR.data(), static_cast<int>(in.size()));
    muteStage.process(in.data(), in.data(), out.data(), outR.data(), static_cast<int>(in.size()));
    const std::vector<float> tail(out.end() - 4800, out.end());
    REQUIRE(peak(tail) < 1e-6f); // smoothed mute settles below -120 dB
}

// process in callback-sized chunks (the stage refreshes EQ coefficients
// per block, like the real audio callback)
void processChunks(OutputStage& stage, const std::vector<float>& in, std::vector<float>& outL,
                   std::vector<float>& outR, int chunk = 64)
{
    REQUIRE(outL.size() == in.size());
    REQUIRE(outR.size() == in.size());
    for (std::size_t off = 0; off < in.size(); off += static_cast<std::size_t>(chunk)) {
        const int n = static_cast<int>(
            std::min(static_cast<std::size_t>(chunk), in.size() - off));
        stage.process(in.data() + off, in.data() + off, outL.data() + off, outR.data() + off, n);
    }
}

TEST_CASE("output stage global EQ shapes the tone")
{
    OutputStage stage;
    stage.prepare(48000.0, 64);
    const std::vector<float> in = sine(48000, 250.0, 48000.0, 0.2f); // bass band
    std::vector<float> out(in.size(), 0.0f);
    std::vector<float> outR(in.size(), 0.0f);

    stage.setBass(1.0f);
    processChunks(stage, in, out, outR);
    const float bassUp = peak(out, 9600);
    stage.reset();
    stage.setBass(0.0f);
    processChunks(stage, in, out, outR);
    const float bassDown = peak(out, 9600);
    REQUIRE(bassUp > bassDown * 2.0f); // +6 vs -6 dB at the shelf corner
}

TEST_CASE("output stage keeps stereo channels symmetric")
{
    OutputStage stage;
    stage.prepare(48000.0, 64);
    const std::vector<float> in = sine(48000, 440.0, 48000.0, 0.15f);
    std::vector<float> outL(in.size(), 0.0f);
    std::vector<float> outR(in.size(), 0.0f);
    stage.setBass(0.7f);
    stage.setMiddle(0.3f);
    processChunks(stage, in, outL, outR);
    for (std::size_t i = 4800; i < outL.size(); ++i) {
        REQUIRE(std::fabs(outL[i] - outR[i]) < 1e-6f);
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("output stage process is allocation free")
{
    OutputStage stage;
    stage.prepare(48000.0, 64);
    std::vector<float> in(256, 0.1f);
    std::vector<float> out(256, 0.0f);
    {
        namfx::rt::ScopedAllocGuard guard;
        for (int b = 0; b < 10; ++b) {
            stage.process(in.data(), in.data(), out.data(), out.data(), 64);
        }
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
