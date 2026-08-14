#include "dsp/tuner.h"
#include "platform/rt_alloc.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using namfx::dsp::Tuner;

std::vector<float> sine(std::size_t n, double freq, double rate, float amp = 0.3f)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = amp * static_cast<float>(
                         std::sin(6.28318530717958647692 * freq * static_cast<double>(i) / rate));
    }
    return x;
}

void feed(Tuner& tuner, const std::vector<float>& in)
{
    for (std::size_t off = 0; off < in.size(); off += 64) {
        tuner.process(in.data() + off, 64);
    }
}

} // namespace

TEST_CASE("tuner detects A4 440 Hz in tune")
{
    Tuner tuner;
    tuner.prepare(48000.0);
    feed(tuner, sine(48000, 440.0, 48000.0));
    REQUIRE(tuner.noteDetected());
    REQUIRE(tuner.midiNote() == 69); // A4
    REQUIRE(std::fabs(tuner.frequency() - 440.0f) < 2.0f);
    REQUIRE(std::fabs(tuner.cents()) < 5.0f);
}

TEST_CASE("tuner detects low E2 and high E6")
{
    Tuner low;
    low.prepare(48000.0);
    feed(low, sine(96000, 82.41, 48000.0)); // E2
    REQUIRE(low.noteDetected());
    REQUIRE(low.midiNote() == 40);

    Tuner high;
    high.prepare(48000.0);
    feed(high, sine(96000, 1318.5, 48000.0)); // E6
    REQUIRE(high.noteDetected());
    REQUIRE(high.midiNote() == 88);
}

TEST_CASE("tuner reports cents deviation")
{
    Tuner tuner;
    tuner.prepare(48000.0);
    feed(tuner, sine(48000, 445.0, 48000.0)); // ~+19.6 cents
    REQUIRE(tuner.noteDetected());
    REQUIRE(std::fabs(tuner.cents() - 19.56f) < 3.0f);
}

TEST_CASE("tuner stays silent on silence and noise-free detection")
{
    Tuner tuner;
    tuner.prepare(48000.0);
    feed(tuner, std::vector<float>(48000, 0.0f));
    REQUIRE_FALSE(tuner.noteDetected());
    REQUIRE(tuner.frequency() == 0.0f);
}

TEST_CASE("tuner handles 96k and 192k device sample rates without overrunning")
{
    // regression: the ACF lag range used to be sized for 48k only; at 96/192k
    // the E2 lag indexed past the fixed buffers (Debug vector bounds -> abort)
    for (double rate : {96000.0, 192000.0}) {
        Tuner tuner;
        tuner.prepare(rate);
        feed(tuner, sine(static_cast<std::size_t>(rate * 3), 440.0, rate));
        REQUIRE(tuner.noteDetected());
        REQUIRE(tuner.midiNote() == 69);
        REQUIRE(std::fabs(tuner.frequency() - 440.0f) < 2.0f);
        // silence must also be safe at these rates
        tuner.prepare(rate);
        feed(tuner, std::vector<float>(static_cast<std::size_t>(rate), 0.0f));
        REQUIRE_FALSE(tuner.noteDetected());
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("tuner process is allocation free")
{
    Tuner tuner;
    tuner.prepare(48000.0);
    const std::vector<float> in = sine(48000, 440.0, 48000.0);
    {
        namfx::rt::ScopedAllocGuard guard;
        feed(tuner, in);
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
