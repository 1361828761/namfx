#include "modules/dsp/bbd_delay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

TEST_CASE("fractional delay passes a constant through at zero delay")
{
    namfx::FractionalDelay delay;
    delay.prepare(48000.0, 30.0f);
    delay.setDelayMs(0.0f);

    float y = 0.0f;
    for (int i = 0; i < 100; ++i) {
        y = delay.process(0.5f);
    }
    REQUIRE(y == Catch::Approx(0.5f).epsilon(1e-6f));
}

TEST_CASE("fractional delay at fixed delay returns the delayed constant")
{
    namfx::FractionalDelay delay;
    delay.prepare(48000.0, 30.0f);
    delay.setDelayMs(1.0f);

    float y = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        y = delay.process(0.25f);
    }
    // after a full buffer pass the whole line holds 0.25
    REQUIRE(y == Catch::Approx(0.25f).epsilon(1e-6f));
}

TEST_CASE("negative delay is clamped to zero instead of reading stale samples")
{
    namfx::FractionalDelay delay;
    delay.prepare(48000.0, 30.0f);
    delay.setDelayMs(-5.0f);

    // with the clamp, a negative delay behaves as zero delay: the output is
    // the just-written sample. Without the clamp it would read ahead into
    // stale buffer content (zeros here), returning 0 instead of 0.5.
    const float y = delay.process(0.5f);
    REQUIRE(y == Catch::Approx(0.5f).epsilon(1e-6f));

    float stale = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        stale = delay.process(0.5f);
    }
    REQUIRE(stale == Catch::Approx(0.5f).epsilon(1e-6f));
}

TEST_CASE("delay near the float rounding boundary never reads out of bounds")
{
    // delaySamples_ = 960.000061: readPos = writeIdx_ - 960.000061 is a tiny
    // negative float that rounds to exactly buffer size after the +size
    // correction when writeIdx_ == 960 - this used to index one past the end
    // of the buffer. With the clamp the read stays in bounds and the output
    // stays bounded for every sample.
    namfx::FractionalDelay delay;
    delay.prepare(48000.0, 30.0f);
    delay.setDelayMs(20.0000013f);

    float y = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        y = delay.process(0.5f);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) <= 1.0f);
    }
}

TEST_CASE("delay beyond the buffer size stays in bounds")
{
    namfx::FractionalDelay delay;
    delay.prepare(48000.0, 10.0f);
    delay.setDelayMs(25.0f); // > maxDelayMs: wraps within the buffer

    float y = 0.0f;
    for (int i = 0; i < 5000; ++i) {
        y = delay.process(0.5f);
        REQUIRE(std::isfinite(y));
    }
    REQUIRE(std::fabs(y) <= 1.0f);
}

TEST_CASE("prepare reallocates when max delay grows and reset clears the line")
{
    namfx::FractionalDelay delay;
    delay.prepare(48000.0, 10.0f);
    delay.setDelayMs(5.0f);
    for (int i = 0; i < 1000; ++i) {
        (void)delay.process(1.0f);
    }

    delay.prepare(48000.0, 60.0f);
    delay.reset();
    // after reset the line is silent: delayed reads return 0
    delay.setDelayMs(5.0f);
    float y = 1.0f;
    for (int i = 0; i < 100; ++i) {
        y = delay.process(0.0f);
    }
    REQUIRE(y == Catch::Approx(0.0f).epsilon(1e-6f));
}

TEST_CASE("triangle lfo runs at the requested rate")
{
    namfx::TriLfo lfo;
    lfo.prepare(48000.0);
    lfo.setRate(2.0f);

    int cycles = 0;
    float prev = lfo.process();
    for (int i = 1; i < 96000; ++i) {
        const float v = lfo.process();
        if (prev > 0.5f && v <= 0.5f) {
            ++cycles;
        }
        prev = v;
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
    // 2 Hz over 2 s = 4 cycles; allow interpolation slack around edges
    REQUIRE(cycles >= 3);
    REQUIRE(cycles <= 5);
}

TEST_CASE("triangle lfo with zero rate stays at zero")
{
    namfx::TriLfo lfo;
    lfo.prepare(48000.0);
    lfo.setRate(0.0f);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(lfo.process() == 0.0f);
    }
}
