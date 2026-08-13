#include "modules/dsp/klon.h"
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

void fillSine(std::vector<float>& buf, float freq, float amp, double sampleRate, double phase = 0.0)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = amp * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / sampleRate + phase));
    }
}

// fundamental + total harmonic distortion of the output relative to the input sine
float measureThd(const std::vector<float>& in, const std::vector<float>& out, float freq, double sampleRate)
{
    // runSweep uses freq = sampleRate / 100, so period is an integer number of
    // samples; both window edges must be on period boundaries or the
    // sin/cos projection leaks and reports a phantom THD. begin is set past
    // the DC blocker transient (35 Hz -> ~217 samples time constant).
    const std::size_t period = static_cast<std::size_t>(sampleRate / static_cast<double>(freq));
    const std::size_t begin = period * 12;
    const std::size_t n = begin + period * 100;
    double sumSin = 0.0, sumCos = 0.0, sumSin2 = 0.0, sumCos2 = 0.0, sumSinCos = 0.0;
    const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
    for (std::size_t i = begin; i < n && i < in.size(); ++i) {
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
    for (std::size_t i = begin; i < n && i < in.size(); ++i) {
        const double s = std::sin(w * static_cast<double>(i));
        const double c = std::cos(w * static_cast<double>(i));
        const double y = a * s + b * c;
        const double e = out[i] - y;
        resid += e * e;
    }
    // THD = residual_rms / fundamental_rms. fund = a^2 + b^2 is the peak
    // amplitude squared, so fundamental_rms = sqrt(fund / 2).
    const double count = static_cast<double>(n - begin);
    const double fund = (a * a + b * b) / 2.0;
    return static_cast<float>(std::sqrt(resid / (count * fund + 1.0e-20)));
}

struct Sweep {
    float peak = 0.0f;
    float thd = 0.0f;
    float rms = 0.0f;
};

Sweep runSweep(std::unique_ptr<namfx::ModuleBase>& mod, float amp, double sampleRate,
               float gain, float treble, float level)
{
    mod->reset();
    mod->setParameter("gain", gain);
    mod->setParameter("treble", treble);
    mod->setParameter("level", level);

    const float freq = static_cast<float>(sampleRate / 100.0);
    std::vector<float> in(16384, 0.0f);
    std::vector<float> out(16384, 0.0f);
    fillSine(in, freq, amp, sampleRate);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    Sweep s;
    s.peak = peakOf(out);
    s.thd = measureThd(in, out, freq, sampleRate);
    for (std::size_t i = 8192; i < out.size(); ++i) {
        s.rms += out[i] * out[i];
    }
    s.rms = std::sqrt(s.rms / static_cast<float>(out.size() - 8192));
    return s;
}

} // namespace

TEST_CASE("transparent registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerTransparent(registry);
    REQUIRE(registry.has("od.transparent"));
    REQUIRE(registry.categoryOf("od.transparent") == "pedal");
    REQUIRE(registry.specsFor("od.transparent").size() == 3);
    REQUIRE(registry.findParam("od.transparent", "gain") != nullptr);
    REQUIRE(registry.findParam("od.transparent", "treble") != nullptr);
    REQUIRE(registry.findParam("od.transparent", "level") != nullptr);

    auto mod = registry.create("od.transparent");
    REQUIRE(mod != nullptr);
}

TEST_CASE("transparent is clean on quiet signals and distorts on loud ones")
{
    namfx::ModuleRegistry registry;
    namfx::registerTransparent(registry);
    auto mod = registry.create("od.transparent");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const Sweep quiet = runSweep(mod, 0.001f, 48000.0, 0.5f, 0.5f, 0.5f);
    const Sweep loud = runSweep(mod, 0.5f, 48000.0, 0.8f, 0.5f, 0.5f);

    // quiet: near-linear pass, bounded output, low distortion (transparent)
    REQUIRE(quiet.peak > 0.00005f);
    REQUIRE(quiet.peak < 0.05f);
    REQUIRE(quiet.thd < 0.05f);

    // loud: heavy clipping, much higher distortion, output bounded by rail clips
    REQUIRE(loud.thd > 0.15f);
    REQUIRE(loud.thd > quiet.thd * 3.0f);
    REQUIRE(loud.peak < 2.0f);
}

TEST_CASE("transparent gain control increases drive monotonically")
{
    namfx::ModuleRegistry registry;
    namfx::registerTransparent(registry);
    auto mod = registry.create("od.transparent");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    // light input: at 0.3 both settings sit deep in the diode saturation
    // plateau, so use 0.05 to keep the drive sweep in the shaping region
    const Sweep g1 = runSweep(mod, 0.05f, 48000.0, 0.2f, 0.5f, 0.5f);
    const Sweep g2 = runSweep(mod, 0.05f, 48000.0, 0.5f, 0.5f, 0.5f);
    const Sweep g3 = runSweep(mod, 0.05f, 48000.0, 0.8f, 0.5f, 0.5f);

    REQUIRE(g2.thd > g1.thd);
    REQUIRE(g3.thd > g2.thd);
}

TEST_CASE("transparent treble control boosts highs at high setting")
{
    namfx::ModuleRegistry registry;
    namfx::registerTransparent(registry);
    auto mod = registry.create("od.transparent");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    auto gainAt = [&mod](float freq, float treble) {
        mod->reset();
        mod->setParameter("gain", 0.5f);
        mod->setParameter("treble", treble);
        mod->setParameter("level", 0.5f);
        std::vector<float> in(16384, 0.0f);
        std::vector<float> out(16384, 0.0f);
        fillSine(in, freq, 0.01f, 48000.0);
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
    REQUIRE(bright8k > dark8k * 1.5f);
    REQUIRE(dark200 > dark8k * 1.5f);
}

TEST_CASE("transparent level control scales output")
{
    namfx::ModuleRegistry registry;
    namfx::registerTransparent(registry);
    auto mod = registry.create("od.transparent");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const Sweep low = runSweep(mod, 0.01f, 48000.0, 0.5f, 0.5f, 0.1f);
    const Sweep high = runSweep(mod, 0.01f, 48000.0, 0.5f, 0.5f, 0.9f);

    REQUIRE(high.peak > low.peak * 3.0f);
}

TEST_CASE("transparent parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerTransparent(registry);
    auto mod = registry.create("od.transparent");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float gains[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float trebles[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float gain : gains) {
        for (float treble : trebles) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("gain", gain);
                mod->setParameter("treble", treble);
                mod->setParameter("level", level);
                mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                for (float v : out) {
                    REQUIRE(std::isfinite(v));
                }
                REQUIRE(peakOf(out) < 20.0f);
            }
        }
    }
}

TEST_CASE("transparent works at 44.1k and 96k sample rates")
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
        namfx::registerTransparent(registry);
        auto mod = registry.create("od.transparent");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("gain", 0.5f);
        mod->setParameter("treble", 0.5f);
        mod->setParameter("level", 0.5f);

        std::vector<float> out(48000, 0.0f);
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
        const float peak = peakOf(out);
        REQUIRE(peak > 0.0f);
        REQUIRE(peak < 20.0f);

        const Sweep quiet = runSweep(mod, 0.001f, rate, 0.5f, 0.5f, 0.5f);
        REQUIRE(quiet.thd < 0.05f);
        const Sweep loud = runSweep(mod, 0.5f, rate, 0.8f, 0.5f, 0.5f);
        REQUIRE(loud.thd > 0.15f);
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("transparent process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerTransparent(registry);
    auto mod = registry.create("od.transparent");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("gain", 0.5f);
    mod->setParameter("treble", 0.5f);
    mod->setParameter("level", 0.5f);

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
