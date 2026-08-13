#include "audio/chain.h"
#include "modules/dsp/gain.h"
#include "modules/dsp/tone.h"
#include "modules/module_registry.h"
#include "preset/preset_io.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;

void writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right)
{
    const std::uint32_t dataSize = static_cast<std::uint32_t>(left.size() * kChannels * 2);
    const std::uint32_t byteRate = static_cast<std::uint32_t>(kSampleRate * kChannels * 2);
    std::ofstream out(path, std::ios::binary);
    const auto writeU32 = [&out](std::uint32_t value) {
        out.put(static_cast<char>(value & 0xFF));
        out.put(static_cast<char>((value >> 8) & 0xFF));
        out.put(static_cast<char>((value >> 16) & 0xFF));
        out.put(static_cast<char>((value >> 24) & 0xFF));
    };
    const auto writeU16 = [&out](std::uint16_t value) {
        out.put(static_cast<char>(value & 0xFF));
        out.put(static_cast<char>((value >> 8) & 0xFF));
    };
    out.write("RIFF", 4);
    writeU32(36 + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(16);
    writeU16(1);
    writeU16(kChannels);
    writeU32(kSampleRate);
    writeU32(byteRate);
    writeU16(static_cast<std::uint16_t>(kChannels * 2));
    writeU16(16);
    out.write("data", 4);
    writeU32(dataSize);
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto toPcm = [](float sample) {
            const float clamped = sample < -1.0f ? -1.0f : (sample > 1.0f ? 1.0f : sample);
            return static_cast<std::int16_t>(clamped * 32767.0f);
        };
        writeU16(static_cast<std::uint16_t>(toPcm(left[i])));
        writeU16(static_cast<std::uint16_t>(toPcm(right[i])));
    }
}

int usage()
{
    std::printf("usage: namfx_render --preset <file.json> [--seconds N] [--freq Hz] [--out out.wav]\n");
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    std::string presetPath;
    std::string outPath = "render.wav";
    double seconds = 2.0;
    double freq = 440.0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--preset" && i + 1 < argc) {
            presetPath = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stod(argv[++i]);
        } else if (arg == "--freq" && i + 1 < argc) {
            freq = std::stod(argv[++i]);
        } else {
            return usage();
        }
    }
    if (presetPath.empty()) {
        return usage();
    }

    std::ifstream file(presetPath, std::ios::binary);
    if (!file) {
        std::printf("error: cannot open preset %s\n", presetPath.c_str());
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto registry = std::make_shared<namfx::ModuleRegistry>();
    namfx::registerGain(*registry);
    namfx::registerTone(*registry);

    namfx::preset::LoadReport report;
    namfx::preset::Preset preset = namfx::preset::loadPreset(
        text, namfx::preset::LoadMode::Strict, *registry, report);
    if (!report.ok()) {
        std::printf("error: preset rejected:");
        for (const std::string& err : report.errors) {
            std::printf(" %s", err.c_str());
        }
        std::printf("\n");
        return 1;
    }

    namfx::audio::Chain chain(preset.chain, registry);
    chain.prepare(kSampleRate, 64);

    const int totalSamples = static_cast<int>(seconds * kSampleRate);
    std::vector<float> inL(64, 0.0f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(static_cast<std::size_t>(totalSamples));
    std::vector<float> outR(static_cast<std::size_t>(totalSamples));
    double phase = 0.0;
    constexpr double kTwoPi = 6.28318530717958647692;
    for (int offset = 0; offset < totalSamples; offset += 64) {
        const int count = totalSamples - offset < 64 ? totalSamples - offset : 64;
        for (int i = 0; i < count; ++i) {
            inL[static_cast<std::size_t>(i)] =
                static_cast<float>(std::sin(phase) * 0.5);
            inR[static_cast<std::size_t>(i)] = 0.0f;
            phase += kTwoPi * freq / kSampleRate;
        }
        chain.process(inL.data(), inR.data(), outL.data() + offset, outR.data() + offset, count);
    }

    writeWav(outPath, outL, outR);
    std::printf("rendered %d samples to %s\n", totalSamples, outPath.c_str());
    return 0;
}
