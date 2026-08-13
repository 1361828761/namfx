#include "modules/dsp/ocd.h"
#include "modules/module_registry.h"
#include "platform/rt_alloc.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

float peakOf(const std::vector<float>& buf)
{
    float peak = 0.0f;
    for (float v : buf) {
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

void fillSine(std::vector<float>& buf, float freq, float amp, double sampleRate)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = amp * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / sampleRate));
    }
}

// projection THD: window edges on period boundaries (freq = fs / 40), past
// the filter transients; THD = residual_rms / fundamental_rms
float measureThd(const std::vector<float>& out, float freq, double sampleRate)
{
    const std::size_t period = static_cast<std::size_t>(sampleRate / static_cast<double>(freq));
    // window starts after parameter smoothing has converged (10 ms tau,
    // ~480 samples at 48k; 300 periods at fs/40 = 250 ms) and spans 250
    // full periods on period boundaries
    const std::size_t begin = period * 300;
    const std::size_t n = begin + period * 250;
    double sumSin = 0.0, sumCos = 0.0, sumSin2 = 0.0, sumCos2 = 0.0, sumSinCos = 0.0;
    const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
    for (std::size_t i = begin; i < n && i < out.size(); ++i) {
        const double s = std::sin(w * static_cast<double>(i));
        const double c = std::cos(w * static_cast<double>(i));
        sumSin += s * out[i];
        sumCos += c * out[i];
        sumSin2 += s * s;
        sumCos2 += c * c;
        sumSinCos += s * c;
    }
    const double denom = sumSin2 * sumCos2 - sumSinCos * sumSinCos;
    const double a = (sumSin * sumCos2 - sumCos * sumSinCos) / denom;
    const double b = (sumCos * sumSin2 - sumSin * sumSinCos) / denom;
    double resid = 0.0;
    for (std::size_t i = begin; i < n && i < out.size(); ++i) {
        const double s = std::sin(w * static_cast<double>(i));
        const double c = std::cos(w * static_cast<double>(i));
        const double y = a * s + b * c;
        const double e = out[i] - y;
        resid += e * e;
    }
    const double count = static_cast<double>(n - begin);
    const double fund = (a * a + b * b) / 2.0;
    return static_cast<float>(std::sqrt(resid / (count * fund + 1.0e-20)));
}

struct Sweep {
    float peak = 0.0f;
    float thd = 0.0f;
};

Sweep runSweep(std::unique_ptr<namfx::ModuleBase>& mod, float amp, double sampleRate,
               float drive, float tone, float volume)
{
    mod->reset();
    mod->setParameter("drive", drive);
    mod->setParameter("tone", tone);
    mod->setParameter("volume", volume);

    const float freq = static_cast<float>(sampleRate / 40.0);
    std::vector<float> in(32768, 0.0f);
    std::vector<float> out(32768, 0.0f);
    fillSine(in, freq, amp, sampleRate);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    Sweep s;
    s.peak = 0.0f;
    for (std::size_t i = 12000; i < out.size(); ++i) {
        s.peak = std::max(s.peak, std::fabs(out[i]));
    }
    s.thd = measureThd(out, freq, sampleRate);
    return s;
}

} // namespace

TEST_CASE("mosfet od registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerMosfetOd(registry);
    REQUIRE(registry.has("od.mosfet"));
    REQUIRE(registry.categoryOf("od.mosfet") == "pedal");
    REQUIRE(registry.specsFor("od.mosfet").size() == 3);
    REQUIRE(registry.findParam("od.mosfet", "drive") != nullptr);
    REQUIRE(registry.findParam("od.mosfet", "tone") != nullptr);
    REQUIRE(registry.findParam("od.mosfet", "volume") != nullptr);

    auto mod = registry.create("od.mosfet");
    REQUIRE(mod != nullptr);
}

TEST_CASE("mosfet od hard-clips loud signals and stays clean on quiet ones")
{
    namfx::ModuleRegistry registry;
    namfx::registerMosfetOd(registry);
    auto mod = registry.create("od.mosfet");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const Sweep quiet = runSweep(mod, 0.001f, 48000.0, 0.2f, 0.5f, 0.5f);
    const Sweep loud = runSweep(mod, 0.5f, 48000.0, 0.8f, 0.5f, 0.5f);

    // quiet: linear small-signal region (below the 2V MOSFET knee)
    REQUIRE(quiet.peak > 0.0f);
    REQUIRE(quiet.thd < 0.05f);

    // loud: heavy square-law clipping, bounded output, much higher distortion
    REQUIRE(loud.thd > 0.2f);
    REQUIRE(loud.thd > quiet.thd * 5.0f);
    REQUIRE(loud.peak < 2.0f);
}

TEST_CASE("mosfet od drive increases distortion monotonically")
{
    namfx::ModuleRegistry registry;
    namfx::registerMosfetOd(registry);
    auto mod = registry.create("od.mosfet");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const Sweep d1 = runSweep(mod, 0.05f, 48000.0, 0.1f, 0.5f, 0.5f);
    const Sweep d2 = runSweep(mod, 0.05f, 48000.0, 0.5f, 0.5f, 0.5f);
    const Sweep d3 = runSweep(mod, 0.05f, 48000.0, 0.9f, 0.5f, 0.5f);

    REQUIRE(d2.thd > d1.thd);
    REQUIRE(d3.thd > d2.thd);
}

TEST_CASE("mosfet od tone control rolls off treble at low setting")
{
    namfx::ModuleRegistry registry;
    namfx::registerMosfetOd(registry);
    auto mod = registry.create("od.mosfet");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    auto gainAt = [&mod](float freq, float tone) {
        mod->reset();
        mod->setParameter("drive", 0.5f);
        mod->setParameter("tone", tone);
        mod->setParameter("volume", 0.5f);
        std::vector<float> in(32768, 0.0f);
        std::vector<float> out(32768, 0.0f);
        fillSine(in, freq, 0.001f, 48000.0);
        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        float peak = 0.0f;
        for (std::size_t i = 8192; i < out.size(); ++i) {
            peak = std::max(peak, std::fabs(out[i]));
        }
        return peak;
    };

    const float dark8k = gainAt(8000.0f, 0.0f);
    const float bright8k = gainAt(8000.0f, 1.0f);
    const float dark200 = gainAt(200.0f, 0.0f);

    REQUIRE(dark8k > 0.0f);
    REQUIRE(bright8k > dark8k * 3.0f);
    REQUIRE(dark200 > dark8k * 2.0f);
}

TEST_CASE("mosfet od volume control scales output")
{
    namfx::ModuleRegistry registry;
    namfx::registerMosfetOd(registry);
    auto mod = registry.create("od.mosfet");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const Sweep low = runSweep(mod, 0.01f, 48000.0, 0.5f, 0.5f, 0.1f);
    const Sweep high = runSweep(mod, 0.01f, 48000.0, 0.5f, 0.5f, 0.9f);

    REQUIRE(high.peak > low.peak * 30.0f);
    REQUIRE(high.peak < low.peak * 200.0f);
}

TEST_CASE("mosfet od parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerMosfetOd(registry);
    auto mod = registry.create("od.mosfet");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float drives[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float tones[] = {0.0f, 0.5f, 1.0f};
    const float volumes[] = {0.0f, 0.5f, 1.0f};

    for (float drive : drives) {
        for (float tone : tones) {
            for (float volume : volumes) {
                mod->reset();
                mod->setParameter("drive", drive);
                mod->setParameter("tone", tone);
                mod->setParameter("volume", volume);
                mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                for (float v : out) {
                    REQUIRE(std::isfinite(v));
                }
                REQUIRE(peakOf(out) < 20.0f);
            }
        }
    }
}

TEST_CASE("mosfet od works at 44.1k and 96k sample rates")
{
    const double rates[] = {44100.0, 96000.0};

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    float dummyR = 0.0f;

    for (double rate : rates) {
        namfx::ModuleRegistry registry;
        namfx::registerMosfetOd(registry);
        auto mod = registry.create("od.mosfet");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("drive", 0.5f);
        mod->setParameter("tone", 0.5f);
        mod->setParameter("volume", 0.5f);

        std::vector<float> out(48000, 0.0f);
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
        const float peak = peakOf(out);
        REQUIRE(peak > 0.0f);
        REQUIRE(peak < 20.0f);

        const Sweep quiet = runSweep(mod, 0.001f, rate, 0.2f, 0.5f, 0.5f);
        REQUIRE(quiet.thd < 0.05f);
        const Sweep loud = runSweep(mod, 0.5f, rate, 0.8f, 0.5f, 0.5f);
        REQUIRE(loud.thd > 0.2f);
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("mosfet od process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerMosfetOd(registry);
    auto mod = registry.create("od.mosfet");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("drive", 0.5f);
    mod->setParameter("tone", 0.5f);
    mod->setParameter("volume", 0.5f);

    std::vector<float> in(512, 0.5f);
    std::vector<float> out(512, 0.0f);
    float dummyR = 0.0f;

    {
        namfx::rt::ScopedAllocGuard guard;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
