#include "modules/ir/cab_ir.h"
#include "modules/ir/fft.h"
#include "modules/ir/min_phase.h"
#include "modules/ir/partitioned_conv.h"
#include "modules/module_registry.h"
#include "platform/rt_alloc.h"
#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

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

double maxAbsError(const std::vector<float>& got, const std::vector<double>& want,
                   std::size_t delay, std::size_t end)
{
    double worst = 0.0;
    for (std::size_t i = delay; i < end && i < got.size(); ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(got[i]) - want[i - delay]));
    }
    return worst;
}

} // namespace

TEST_CASE("fft round trips through inverse transform")
{
    std::vector<std::complex<double>> a(1024);
    std::mt19937 rng(42);
    for (std::complex<double>& v : a) {
        v = std::complex<double>(static_cast<double>(rng() % 1000) / 500.0 - 1.0,
                                 static_cast<double>(rng() % 1000) / 500.0 - 1.0);
    }
    const std::vector<std::complex<double>> original = a;
    namfx::ir::fftInPlace(a, false);
    namfx::ir::fftInPlace(a, true);
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::abs(a[i] - original[i]) < 1e-9);
    }
}

TEST_CASE("partitioned convolution matches the double reference")
{
    std::mt19937 rng(7);
    std::vector<float> ir(16384);
    for (float& v : ir) {
        v = static_cast<float>(rng() % 2000) / 1000.0f - 1.0f;
    }
    std::vector<float> in(32768);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.5f * std::sin(0.03f * static_cast<float>(i))
            + 0.3f * std::sin(0.011f * static_cast<float>(i));
    }

    namfx::ir::PartitionedConvolver conv;
    conv.prepare(ir, 1024);
    std::vector<float> out(in.size(), 0.0f);
    conv.process(in.data(), static_cast<int>(in.size()), out.data());

    // the partitioned output lags by one block (1024); compare aligned
    const std::vector<double> want = referenceConv(in, ir);
    const std::size_t delay = 1023;
    REQUIRE(maxAbsError(out, want, delay, in.size() - 4096) < 1e-4);
}

TEST_CASE("partitioned convolution is stream-agnostic to block sizes")
{
    std::mt19937 rng(11);
    std::vector<float> ir(8192);
    for (float& v : ir) {
        v = static_cast<float>(rng() % 2000) / 1000.0f - 1.0f;
    }
    std::vector<float> in(20000);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.4f * std::sin(0.05f * static_cast<float>(i));
    }

    // one shot
    namfx::ir::PartitionedConvolver whole;
    whole.prepare(ir, 1024);
    std::vector<float> outWhole(in.size(), 0.0f);
    whole.process(in.data(), static_cast<int>(in.size()), outWhole.data());

    // chopped into irregular chunks (64, 100, 511, 1024, 7, ...)
    namfx::ir::PartitionedConvolver chopped;
    chopped.prepare(ir, 1024);
    std::vector<float> outChop(in.size(), 0.0f);
    std::size_t pos = 0;
    const int chunkSizes[] = {64, 100, 511, 1024, 7, 777, 512, 300};
    int c = 0;
    while (pos < in.size()) {
        const int chunk = std::min(chunkSizes[c % 8], static_cast<int>(in.size() - pos));
        chopped.process(in.data() + pos, chunk, outChop.data() + pos);
        pos += static_cast<std::size_t>(chunk);
        ++c;
    }
    REQUIRE(maxAbsError(outChop, std::vector<double>(outWhole.begin(), outWhole.end()), 0,
                        in.size()) < 1e-6f);
}

TEST_CASE("cab ir uses partitioned convolution for long impulse responses")
{
    namfx::ModuleRegistry registry;
    namfx::registerCabIr(registry);

    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("namfx_part_" + std::to_string(static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(dir);

    // 5000 samples (> 4096 direct limit) -> partitioned path
    std::vector<float> ir(5000);
    std::mt19937 rng(23);
    for (std::size_t i = 0; i < ir.size(); ++i) {
        ir[i] = std::exp(-0.0006f * static_cast<float>(i))
            * (static_cast<float>(rng() % 2000) / 1000.0f - 1.0f);
    }
    const std::filesystem::path file = dir / "long.wav";
    testx::WavFixture::writeFile(file.string(),
                                 testx::WavFixture::makeWav(3, 32, 1, 48000, ir));

    auto mod = registry.create("cab.ir");
    REQUIRE(mod != nullptr);
    REQUIRE(mod->loadAsset(file.string()));
    mod->prepare(48000.0, 32768); // maxBlock must cover the processing call
    mod->setParameter("gain", 0.5f); // 0 dB

    std::vector<float> in(30000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.4f * std::sin(0.03f * static_cast<float>(i));
    }
    std::vector<float> out(in.size(), 0.0f);
    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // partitioned path: output lags by one 1024 block; cab.ir applies
    // minimum phase + peak normalization on load, mirror that here
    const std::vector<float> mp = namfx::ir::minimumPhase(ir);
    float peak = 0.0f;
    for (float v : mp) {
        peak = std::max(peak, std::fabs(v));
    }
    std::vector<float> normIr(mp.size());
    for (std::size_t i = 0; i < mp.size(); ++i) {
        normIr[i] = mp[i] / peak;
    }
    const std::vector<double> want = referenceConv(in, normIr);
    REQUIRE(maxAbsError(out, want, 1023, in.size() - 4096) < 1e-4);
    std::filesystem::remove_all(dir);
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("partitioned convolution process is allocation free")
{
    std::vector<float> ir(16384, 0.01f);
    namfx::ir::PartitionedConvolver conv;
    conv.prepare(ir, 1024);
    std::vector<float> in(2048, 0.3f);
    std::vector<float> out(2048, 0.0f);

    {
        namfx::rt::ScopedAllocGuard guard;
        conv.process(in.data(), static_cast<int>(in.size()), out.data());
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
