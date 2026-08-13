// Golden file generator: renders the ts808 scenario with the current
// implementation and writes the reference samples. Run manually whenever the
// DSP implementation changes; commit the regenerated file. The comparison
// test fails when the golden file is missing.
#include "audio/chain.h"
#include "audio/slot.h"
#include "modules/dsp/ts808.h"
#include "modules/module_registry.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifndef NAMFX_GOLDEN_DIR
#define NAMFX_GOLDEN_DIR "data"
#endif

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 2;
constexpr int kBlock = 64;
constexpr double kTwoPi = 6.28318530717958647692;
const char* kGoldenName = "ts808_drive5_tone5_level0.f32";

} // namespace

int main()
{
    auto registry = std::make_shared<namfx::ModuleRegistry>();
    namfx::registerTs808(*registry);

    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "od.ts808";
    def.params.push_back(namfx::ParamInit{"drive", 5.0f});
    def.params.push_back(namfx::ParamInit{"tone", 5.0f});
    def.params.push_back(namfx::ParamInit{"level", 0.0f});
    slots.push_back(std::move(def));

    namfx::audio::Chain chain(std::move(slots), registry);
    chain.prepare(kSampleRate, kBlock);

    const int total = kSeconds * static_cast<int>(kSampleRate);
    std::vector<float> in(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> out(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    std::vector<float> samples(static_cast<std::size_t>(total));

    int sample = 0;
    for (int offset = 0; offset < total; offset += kBlock) {
        for (int i = 0; i < kBlock; ++i) {
            const double t = static_cast<double>(sample) / kSampleRate;
            in[static_cast<std::size_t>(i)] =
                static_cast<float>(0.3 * std::sin(kTwoPi * 220.0 * t));
            ++sample;
        }
        chain.process(in.data(), inR.data(), out.data(), outR.data(), kBlock);
        std::memcpy(samples.data() + offset, out.data(), sizeof(float) * kBlock);
    }

    const std::string path = std::string(NAMFX_GOLDEN_DIR) + "/" + kGoldenName;
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::printf("error: cannot write %s\n", path.c_str());
        return 1;
    }
    file.write(reinterpret_cast<const char*>(samples.data()),
               static_cast<std::streamsize>(samples.size() * sizeof(float)));
    file.close();

    double peak = 0.0;
    for (float s : samples) {
        peak = std::max(peak, std::fabs(static_cast<double>(s)));
    }
    std::printf("wrote %s: %zu samples, peak %.6f\n", path.c_str(), samples.size(), peak);
    return 0;
}
