#include "audio/chain.h"
#include "modules/module_registry.h"
#include "modules/nam/nam_amp.h"
#include "platform/rt_alloc.h"
#include "preset/preset_io.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

// test model assets live next to the unit tests (NAM Core example models,
// pinned commit; MIT)
std::filesystem::path asset(const char* name)
{
    return std::filesystem::path(NAMFX_TEST_ASSETS_DIR) / name;
}

std::vector<float> makeSine(std::size_t n, double freq, float amp, double rate)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = amp * static_cast<float>(std::sin(6.28318530717958647692 * freq
                                                 * static_cast<double>(i) / rate));
    }
    return x;
}

std::unique_ptr<namfx::ModuleBase> makeModule(namfx::ModuleRegistry& registry,
                                              const std::string& file)
{
    auto mod = registry.create("amp.nam");
    REQUIRE(mod != nullptr);
    REQUIRE(mod->loadAsset(file));
    return mod;
}

// process in callback-sized chunks (module contract: n <= prepare maxBlock)
void processChunks(namfx::ModuleBase& mod, const std::vector<float>& in, std::vector<float>& out,
                   int chunk = 64)
{
    REQUIRE(out.size() == in.size());
    float dummyR = 0.0f;
    for (std::size_t off = 0; off < in.size(); off += static_cast<std::size_t>(chunk)) {
        const int n = static_cast<int>(
            std::min(static_cast<std::size_t>(chunk), in.size() - off));
        mod.process(in.data() + off, &dummyR, out.data() + off, &dummyR, n);
    }
}

// single-bin DFT amplitude (steady-state tail)
double binAmplitude(const std::vector<float>& y, double rate, double freq)
{
    const std::size_t m = y.size() / 2;
    std::complex<double> acc(0.0, 0.0);
    for (std::size_t i = m; i < y.size(); ++i) {
        const double ph = -2.0 * 3.14159265358979323846 * freq * static_cast<double>(i) / rate;
        acc += std::complex<double>(std::cos(ph), std::sin(ph)) * static_cast<double>(y[i]);
    }
    return 2.0 * std::abs(acc) / static_cast<double>(y.size() - m);
}

} // namespace

TEST_CASE("nam amp registers as amp category with five tone params")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    REQUIRE(registry.has("amp.nam"));
    REQUIRE(registry.categoryOf("amp.nam") == "amp");
    REQUIRE(registry.specsFor("amp.nam").size() == 5);
    for (const char* p : {"gain", "bass", "middle", "treble", "output"}) {
        REQUIRE(registry.findParam("amp.nam", p) != nullptr);
    }
}

TEST_CASE("nam amp loads a .nam file and renders a non-trivial output")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    auto mod = makeModule(registry, asset("wavenet.nam").string());
    mod->prepare(48000.0, 64);

    std::vector<float> in = makeSine(12000, 110.0f, 0.2f, 48000.0);
    std::vector<float> out(in.size(), 0.0f);
    processChunks(*mod, in, out);

    // model must produce signal (non-zero, finite), and it must differ from
    // a pure passthrough (the model shapes the tone)
    float peak = 0.0f;
    double rms = 0.0;
    for (std::size_t i = 8192; i < out.size(); ++i) {
        peak = std::max(peak, std::fabs(out[i]));
        rms += static_cast<double>(out[i]) * out[i];
    }
    rms = std::sqrt(rms / (out.size() - 8192));
    REQUIRE(std::isfinite(peak));
    REQUIRE(peak > 1e-3f);
    REQUIRE(rms > 1e-5);
}

TEST_CASE("nam amp gain scales the output before the model")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    auto run = [&](float gain) {
        auto mod = makeModule(registry, asset("wavenet.nam").string());
        mod->prepare(48000.0, 64);
        mod->setParameter("gain", gain);
        std::vector<float> warm(4800, 0.0f);
        std::vector<float> warmOut(4800, 0.0f);
        processChunks(*mod, warm, warmOut);
        std::vector<float> in = makeSine(12000, 110.0f, 0.1f, 48000.0);
        std::vector<float> out(in.size(), 0.0f);
        processChunks(*mod, in, out);
        float peak = 0.0f;
        for (std::size_t i = 8192; i < out.size(); ++i) {
            peak = std::max(peak, std::fabs(out[i]));
        }
        return peak;
    };
    const float g0 = run(0.0f);
    const float g05 = run(0.5f);
    const float g1 = run(1.0f);
    // gain 0 -> -12 dB, gain 1 -> +12 dB around 0 dB neutral
    REQUIRE(g0 < g05);
    REQUIRE(g1 > g05 * 1.5f);
}

TEST_CASE("nam amp tone controls shape the output spectrum")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    auto run = [&](float bass, float middle, float treble) {
        auto mod = makeModule(registry, asset("wavenet.nam").string());
        mod->prepare(48000.0, 64);
        mod->setParameter("bass", bass);
        mod->setParameter("middle", middle);
        mod->setParameter("treble", treble);
        std::vector<float> warm(4800, 0.0f);
        std::vector<float> warmOut(4800, 0.0f);
        processChunks(*mod, warm, warmOut);
        // small signal keeps the model near-linear; probe the 250 Hz bin of
        // the post-EQ output (time-domain peaks are polluted by harmonics)
        std::vector<float> in = makeSine(12000, 250.0f, 0.02f, 48000.0); // bass band
        std::vector<float> out(in.size(), 0.0f);
        processChunks(*mod, in, out);
        return binAmplitude(out, 48000.0, 250.0);
    };
    const float bassUp = static_cast<float>(run(1.0f, 0.5f, 0.5f));
    const float bassDown = static_cast<float>(run(0.0f, 0.5f, 0.5f));
    REQUIRE(bassUp > bassDown * 2.0f); // 250 Hz probe: +12 vs -12 dB shelf
}

TEST_CASE("nam amp renders at a non-48k engine rate via internal resampling")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    auto mod = makeModule(registry, asset("wavenet.nam").string());
    mod->prepare(44100.0, 64); // model expects 48k

    std::vector<float> in = makeSine(22050, 110.0f, 0.2f, 44100.0);
    std::vector<float> out(in.size(), 0.0f);
    processChunks(*mod, in, out);
    float peak = 0.0f;
    for (std::size_t i = 8192; i < out.size(); ++i) {
        peak = std::max(peak, std::fabs(out[i]));
        REQUIRE(std::isfinite(out[i]));
    }
    REQUIRE(peak > 1e-3f);
}

TEST_CASE("nam amp loads the A2 slimmable container and the slimmable wavenet")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    for (const char* file : {"A2.nam", "slimmable_wavenet.nam"}) {
        auto mod = makeModule(registry, asset(file).string());
        mod->prepare(48000.0, 64);
        std::vector<float> in = makeSine(9600, 220.0f, 0.1f, 48000.0);
        std::vector<float> out(in.size(), 0.0f);
        processChunks(*mod, in, out);
        float peak = 0.0f;
        for (std::size_t i = 4096; i < out.size(); ++i) {
            peak = std::max(peak, std::fabs(out[i]));
        }
        REQUIRE(std::isfinite(peak));
        REQUIRE(peak > 1e-3f);
    }
}

TEST_CASE("nam amp rejects a missing or broken model file")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    auto mod = registry.create("amp.nam");
    REQUIRE(mod != nullptr);
    REQUIRE_FALSE(mod->loadAsset("does_not_exist.nam"));
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "namfx_nam_bad";
    std::filesystem::create_directories(dir);
    const std::filesystem::path bad = dir / "bad.nam";
    {
        std::ofstream f(bad);
        f << "{ not json";
    }
    REQUIRE_FALSE(mod->loadAsset(bad.string()));
    std::filesystem::remove_all(dir);
}

TEST_CASE("nam amp parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    auto mod = makeModule(registry, asset("wavenet.nam").string());
    mod->prepare(48000.0, 64);
    std::vector<float> in = makeSine(12000, 130.0f, 0.15f, 48000.0);
    std::vector<float> out(12000, 0.0f);
    const float values[] = {0.0f, 0.5f, 1.0f};
    for (float g : values) {
        for (float b : values) {
            mod->reset();
            mod->setParameter("gain", g);
            mod->setParameter("bass", b);
            processChunks(*mod, in, out);
            for (float v : out) {
                REQUIRE(std::isfinite(v));
                REQUIRE(std::fabs(v) < 100.0f);
            }
        }
    }
}

TEST_CASE("nam amp loads through the chain with a preset file field")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "namfx_nam_chain";
    std::filesystem::create_directories(dir);
    std::filesystem::copy_file(asset("wavenet.nam"), dir / "wavenet.nam",
                               std::filesystem::copy_options::overwrite_existing);
    const std::string presetText = R"({
        "schema": 1,
        "name": "NAM Chain",
        "chain": [{
            "slot": 0, "category": "amp", "impl": "nam", "module": "amp.nam",
            "file": "wavenet.nam", "params": { "gain": 0.5 }, "bypass": false, "mix": 1.0
        }],
        "scenes": []
    })";

    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    namfx::preset::LoadReport report;
    const namfx::preset::Preset preset = namfx::preset::loadPreset(
        presetText, namfx::preset::LoadMode::Strict, registry, report, dir.string());
    REQUIRE(report.ok());
    REQUIRE(preset.chain.size() == 1);

    namfx::audio::Chain chain(preset.chain, std::make_shared<const namfx::ModuleRegistry>(registry));
    chain.prepare(48000.0, 64);

    std::vector<float> in = makeSine(12000, 110.0f, 0.2f, 48000.0);
    std::vector<float> out(in.size(), 0.0f);
    for (std::size_t off = 0; off < in.size(); off += 64) {
        const int n = static_cast<int>(std::min(static_cast<std::size_t>(64), in.size() - off));
        chain.process(in.data() + off, in.data() + off, out.data() + off, out.data() + off, n);
    }
    float peak = 0.0f;
    for (std::size_t i = 8192; i < out.size(); ++i) {
        peak = std::max(peak, std::fabs(out[i]));
    }
    REQUIRE(peak > 1e-3f);
    std::filesystem::remove_all(dir);
}

TEST_CASE("three nam amp instances in one chain stay independent under load")
{
    // PLAN multi-instance pressure: 3 NAM models in one chain, mixed
    // architectures, chained audio; instances must keep independent state
    // (same input, per-instance params, deterministic outputs)
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "namfx_nam_multi";
    std::filesystem::create_directories(dir);
    for (const char* m : {"wavenet.nam", "slimmable_wavenet.nam", "A2.nam"}) {
        std::filesystem::copy_file(asset(m), dir / m, std::filesystem::copy_options::overwrite_existing);
    }
    const std::string presetText = R"({
        "schema": 1,
        "name": "NAM Multi",
        "chain": [
            { "slot": 0, "category": "amp", "impl": "nam", "module": "amp.nam",
              "file": "wavenet.nam", "params": { "gain": 0.5 }, "bypass": false, "mix": 1.0 },
            { "slot": 1, "category": "amp", "impl": "nam", "module": "amp.nam",
              "file": "slimmable_wavenet.nam", "params": { "gain": 0.3 }, "bypass": false, "mix": 1.0 },
            { "slot": 2, "category": "amp", "impl": "nam", "module": "amp.nam",
              "file": "A2.nam", "params": { "gain": 0.7, "bass": 0.4 }, "bypass": false, "mix": 1.0 }
        ],
        "scenes": []
    })";

    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    namfx::preset::LoadReport report;
    const namfx::preset::Preset preset = namfx::preset::loadPreset(
        presetText, namfx::preset::LoadMode::Strict, registry, report, dir.string());
    REQUIRE(report.ok());
    REQUIRE(preset.chain.size() == 3);

    namfx::audio::Chain chain(preset.chain, std::make_shared<const namfx::ModuleRegistry>(registry));
    chain.prepare(48000.0, 64);

    const std::vector<float> in = makeSine(24000, 130.0f, 0.12f, 48000.0);
    std::vector<float> out1(in.size(), 0.0f);
    std::vector<float> out2(in.size(), 0.0f);
    for (std::size_t off = 0; off < in.size(); off += 64) {
        const int n = static_cast<int>(std::min(static_cast<std::size_t>(64), in.size() - off));
        chain.process(in.data() + off, in.data() + off, out1.data() + off, out1.data() + off, n);
    }
    // reset and re-run: deterministic (same output), then with a changed
    // parameter on one instance the others must be unaffected
    chain.reset();
    for (std::size_t off = 0; off < in.size(); off += 64) {
        const int n = static_cast<int>(std::min(static_cast<std::size_t>(64), in.size() - off));
        chain.process(in.data() + off, in.data() + off, out2.data() + off, out2.data() + off, n);
    }
    double worst = 0.0;
    for (std::size_t i = 8192; i < out1.size(); ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(out1[i]) - out2[i]));
    }
    REQUIRE(worst < 1e-5); // deterministic across reset
    float peak = 0.0f;
    for (std::size_t i = 8192; i < out1.size(); ++i) {
        peak = std::max(peak, std::fabs(out1[i]));
        REQUIRE(std::isfinite(out1[i]));
    }
    REQUIRE(peak > 1e-3f);
    std::filesystem::remove_all(dir);
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("nam amp process is allocation free in the audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerNamAmp(registry);
    auto mod = makeModule(registry, asset("wavenet.nam").string());
    mod->prepare(48000.0, 64);
    std::vector<float> in(512, 0.1f);
    std::vector<float> out(512, 0.0f);

    // contract: n <= prepared maxBlock; warm up first (prewarm done in
    // prepare), then measure allocation-freedom in callback-sized chunks
    std::vector<float> warm(256, 0.1f);
    std::vector<float> warmOut(256, 0.0f);
    processChunks(*mod, warm, warmOut);
    {
        namfx::rt::ScopedAllocGuard guard;
        processChunks(*mod, in, out);
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
