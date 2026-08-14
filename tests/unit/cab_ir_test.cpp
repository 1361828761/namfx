#include "audio/chain.h"
#include "modules/ir/cab_ir.h"
#include "modules/ir/resample.h"
#include "modules/module_registry.h"
#include "platform/rt_alloc.h"
#include "preset/preset_io.h"
#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::filesystem::path tempDir()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("namfx_ir_" + std::to_string(static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(dir);
    return dir;
}

// write a mono wav with the given samples; returns the file path
std::filesystem::path writeIrWav(const std::filesystem::path& dir, const std::string& name,
                                 int sampleRate, const std::vector<float>& samples, bool pcm16)
{
    const std::filesystem::path file = dir / name;
    testx::WavFixture::writeFile(file.string(),
                                 testx::WavFixture::makeWav(pcm16 ? 1 : 3, pcm16 ? 16 : 32, 1,
                                                            sampleRate, samples));
    return file;
}

std::unique_ptr<namfx::ModuleBase> makeModule(namfx::ModuleRegistry& registry)
{
    auto mod = registry.create("cab.ir");
    REQUIRE(mod != nullptr);
    return mod;
}

// reference direct convolution in double precision
std::vector<double> referenceConv(const std::vector<float>& in, const std::vector<float>& ir)
{
    std::vector<double> out(in.size(), 0.0);
    for (std::size_t n = 0; n < in.size(); ++n) {
        double acc = 0.0;
        for (std::size_t k = 0; k < ir.size() && k <= n; ++k) {
            acc += static_cast<double>(ir[k]) * static_cast<double>(in[n - k]);
        }
        out[n] = acc;
    }
    return out;
}

double maxAbsError(const std::vector<float>& got, const std::vector<double>& want)
{
    double worst = 0.0;
    const std::size_t n = std::min(got.size(), want.size());
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(got[i]) - want[i]));
    }
    return worst;
}

} // namespace

TEST_CASE("cab ir registers as cab category with gain, lowcut, highcut params")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    REQUIRE(registry.has("cab.ir"));
    REQUIRE(registry.categoryOf("cab.ir") == "cab");
    REQUIRE(registry.specsFor("cab.ir").size() == 3);
    REQUIRE(registry.findParam("cab.ir", "gain") != nullptr);
    REQUIRE(registry.findParam("cab.ir", "lowcut") != nullptr);
    REQUIRE(registry.findParam("cab.ir", "highcut") != nullptr);
    REQUIRE(makeModule(registry) != nullptr);
}

TEST_CASE("cab ir lowcut and highcut shape the tone")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    const std::filesystem::path dir = tempDir();
    const std::vector<float> ir = {1.0f}; // pure passthrough kernel
    const std::filesystem::path file = writeIrWav(dir, "tone.wav", 48000, ir, false);

    auto gainAt = [&](float lowcut, float highcut, float freq) {
        auto mod = makeModule(registry);
        REQUIRE(mod->loadAsset(file.string()));
        mod->prepare(48000.0, 64);
        mod->setParameter("gain", 0.5f);
        mod->setParameter("lowcut", lowcut);
        mod->setParameter("highcut", highcut);
        std::vector<float> warm(4800, 0.0f);
        std::vector<float> warmOut(4800, 0.0f);
        float dummyR = 0.0f;
        mod->process(warm.data(), &dummyR, warmOut.data(), &dummyR, static_cast<int>(warm.size()));
        std::vector<float> in(12000, 0.0f);
        std::vector<float> out(12000, 0.0f);
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = 0.4f
                * static_cast<float>(std::sin(6.28318530717958647692 * freq * i / 48000.0));
        }
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        float peak = 0.0f;
        for (std::size_t i = 6000; i < out.size(); ++i) {
            peak = std::max(peak, std::fabs(out[i]));
        }
        return peak;
    };

    // lowcut 1 -> 200 Hz high-pass: 100 Hz strongly cut, 1 kHz untouched
    REQUIRE(gainAt(1.0f, 1.0f, 100.0f) < gainAt(1.0f, 1.0f, 1000.0f) * 0.1f);
    // highcut 0 -> 4 kHz low-pass: 8 kHz cut to ~21%, 1 kHz untouched
    REQUIRE(gainAt(0.0f, 0.0f, 8000.0f) < gainAt(0.0f, 0.0f, 1000.0f) * 0.25f);
    std::filesystem::remove_all(dir);
}

TEST_CASE("cab ir convolves within -100 dB of the double reference")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);

    const std::filesystem::path dir = tempDir();
    const std::vector<float> ir = {0.5f, 0.25f, 0.125f, -0.0625f, 0.03125f};
    const std::filesystem::path file = writeIrWav(dir, "short.wav", 48000, ir, false); // float32, lossless

    auto mod = makeModule(registry);
    REQUIRE(mod->loadAsset(file.string()));
    mod->prepare(48000.0, 64);
    mod->setParameter("gain", 0.5f); // 0 dB

    std::vector<float> in(2000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.3f * std::sin(0.05f * static_cast<float>(i))
            + 0.2f * std::sin(0.013f * static_cast<float>(i));
    }
    std::vector<float> out(in.size(), 0.0f);
    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // gain 0.5 -> 0 dB (no scale); -100 dB = 1e-5 relative to peak
    const std::vector<double> want = referenceConv(in, ir);
    REQUIRE(maxAbsError(out, want) < 1e-5);
    std::filesystem::remove_all(dir);
}

TEST_CASE("cab ir resamples a 24 kHz IR to the engine rate")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);

    const std::filesystem::path dir = tempDir();
    // a smooth low-frequency IR: resampling must stay close to the double
    // linear-interpolation reference
    std::vector<float> ir(64);
    for (std::size_t i = 0; i < ir.size(); ++i) {
        ir[i] = std::exp(-0.02f * static_cast<float>(i)) * std::sin(0.1f * static_cast<float>(i));
    }
    const std::filesystem::path file = writeIrWav(dir, "ir24k.wav", 24000, ir, false); // float32

    auto mod = makeModule(registry);
    REQUIRE(mod->loadAsset(file.string()));
    mod->prepare(48000.0, 64);
    mod->setParameter("gain", 0.5f);

    std::vector<float> in(2000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.3f * std::sin(0.05f * static_cast<float>(i));
    }
    std::vector<float> out(in.size(), 0.0f);
    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // reference: double resample + double convolution
    const std::vector<float> ir48 = namfx::ir::resampleLinear(ir, 24000.0, 48000.0);
    const std::vector<double> want = referenceConv(in, ir48);
    REQUIRE(maxAbsError(out, want) < 1e-5);
    std::filesystem::remove_all(dir);
}

TEST_CASE("cab ir gain scales the output in dB")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);

    const std::filesystem::path dir = tempDir();
    const std::vector<float> ir = {0.5f};
    const std::filesystem::path file = writeIrWav(dir, "one.wav", 48000, ir, false);

    auto gainPeak = [&](float gain) {
        auto mod = makeModule(registry);
        REQUIRE(mod->loadAsset(file.string()));
        mod->prepare(48000.0, 64);
        mod->setParameter("gain", gain);
        // let the 10 ms gain smoothing settle first
        std::vector<float> warm(4800, 0.0f);
        std::vector<float> warmOut(4800, 0.0f);
        float dummyR = 0.0f;
        mod->process(warm.data(), &dummyR, warmOut.data(), &dummyR, static_cast<int>(warm.size()));
        std::vector<float> in(4096, 0.0f);
        std::vector<float> out(4096, 0.0f);
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = 0.4f * std::sin(0.1f * static_cast<float>(i));
        }
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        float peak = 0.0f;
        for (float v : out) {
            peak = std::max(peak, std::fabs(v));
        }
        return peak;
    };

    // gain 0 -> -12 dB (10^-0.6 = 0.2512x), gain 1 -> +12 dB, gain 0.5 -> 0 dB
    const float g0 = gainPeak(0.0f);
    const float g05 = gainPeak(0.5f);
    const float g1 = gainPeak(1.0f);
    REQUIRE(std::fabs(g0 / g05 - 0.2512f) < 1e-3f);
    REQUIRE(g1 / g05 > 2.5f);
    std::filesystem::remove_all(dir);
}

TEST_CASE("cab ir rejects oversized impulse responses")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    const std::filesystem::path dir = tempDir();
    const std::filesystem::path file = writeIrWav(dir, "huge.wav", 48000,
                                                  std::vector<float>(70000, 0.001f), true);
    auto mod = makeModule(registry);
    REQUIRE_FALSE(mod->loadAsset(file.string())); // rejected, never truncated
    std::filesystem::remove_all(dir);
}

TEST_CASE("cab ir without an asset passes a unit kernel")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    auto mod = makeModule(registry);
    mod->prepare(48000.0, 64);
    mod->setParameter("gain", 0.5f); // 0 dB

    std::vector<float> in(512, 0.0f);
    std::vector<float> out(512, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.3f * std::sin(0.1f * static_cast<float>(i));
    }
    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    for (std::size_t i = 0; i < 512; ++i) {
        REQUIRE(std::fabs(out[i] - in[i]) < 1e-6f);
    }
}

TEST_CASE("cab ir loads through the chain with a preset file field")
{
    const std::filesystem::path dir = tempDir();
    const std::vector<float> ir = {0.5f, 0.25f};
    const std::filesystem::path irFile = writeIrWav(dir, "chain.wav", 48000, ir, false);

    const std::string presetText = R"({
        "schema": 1,
        "name": "IR Chain",
        "chain": [{
            "slot": 0, "category": "cab", "impl": "ir", "module": "cab.ir",
            "file": "chain.wav", "params": { "gain": 0.5 }, "bypass": false, "mix": 1.0
        }],
        "scenes": []
    })";

    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    namfx::preset::LoadReport report;
    const namfx::preset::Preset preset = namfx::preset::loadPreset(
        presetText, namfx::preset::LoadMode::Strict, registry, report, dir.string());
    REQUIRE(report.ok());
    REQUIRE(preset.chain.size() == 1);
    REQUIRE(preset.chain[0].file == (dir / "chain.wav").string());

    namfx::audio::Chain chain(preset.chain, std::make_shared<const namfx::ModuleRegistry>(registry));
    chain.prepare(48000.0, 4096);

    std::vector<float> in(1000, 0.0f);
    std::vector<float> out(1000, 0.0f);
    in[0] = 1.0f;
    chain.process(in.data(), in.data(), out.data(), out.data(), 1000);
    // impulse convolved with [0.5, 0.25] at 0 dB gain
    REQUIRE(std::fabs(out[0] - 0.5f) < 1e-5f);
    REQUIRE(std::fabs(out[1] - 0.25f) < 1e-5f);
    std::filesystem::remove_all(dir);
}

TEST_CASE("cab ir parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    const std::filesystem::path dir = tempDir();
    std::vector<float> ir(256);
    for (std::size_t i = 0; i < ir.size(); ++i) {
        ir[i] = std::exp(-0.01f * static_cast<float>(i)) * (0.5f - static_cast<float>(i % 2));
    }
    const std::filesystem::path file = writeIrWav(dir, "sweep.wav", 48000, ir, true);

    auto mod = makeModule(registry);
    REQUIRE(mod->loadAsset(file.string()));
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    for (int round = 0; round < 4; ++round) {
        mod->reset();
        mod->setParameter("gain", static_cast<float>(round) / 3.0f);
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("cab ir works at 44.1k and 96k sample rates")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    const std::filesystem::path dir = tempDir();
    std::vector<float> ir(64);
    for (std::size_t i = 0; i < ir.size(); ++i) {
        ir[i] = std::exp(-0.02f * static_cast<float>(i)) * std::sin(0.1f * static_cast<float>(i));
    }
    const std::filesystem::path file = writeIrWav(dir, "rates.wav", 48000, ir, true);

    for (double rate : {44100.0, 96000.0}) {
        auto mod = makeModule(registry);
        REQUIRE(mod->loadAsset(file.string()));
        mod->prepare(rate, 64);
        mod->setParameter("gain", 0.5f);

        std::vector<float> in(24000, 0.0f);
        std::vector<float> out(24000, 0.0f);
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = 0.4f * std::sin(0.1f * static_cast<float>(i));
        }
        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
        float peak = 0.0f;
        for (float v : out) {
            peak = std::max(peak, std::fabs(v));
        }
        REQUIRE(peak > 0.01f);
    }
    std::filesystem::remove_all(dir);
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("cab ir process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);
    const std::filesystem::path dir = tempDir();
    const std::filesystem::path file = writeIrWav(dir, "rt.wav", 48000,
                                                  std::vector<float>(256, 0.001f), true);
    auto mod = makeModule(registry);
    REQUIRE(mod->loadAsset(file.string()));
    mod->prepare(48000.0, 64);
    mod->setParameter("gain", 0.5f);

    std::vector<float> in(512, 0.3f);
    std::vector<float> out(512, 0.0f);
    float dummyR = 0.0f;

    {
        namfx::rt::ScopedAllocGuard guard;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        REQUIRE_FALSE(guard.violated());
    }
    std::filesystem::remove_all(dir);
}

#endif
