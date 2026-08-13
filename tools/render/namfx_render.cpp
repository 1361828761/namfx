#include "audio/chain.h"
#include "modules/dsp/gain.h"
#include "modules/dsp/tone.h"
#include "modules/dsp/ts808.h"
#include "modules/dsp/klon.h"
#include "modules/dsp/ocd.h"
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

constexpr int kChannels = 2;

struct AudioInput {
    std::vector<float> left;
    std::vector<float> right;
};

bool readWav(const std::string& path, AudioInput& input, int targetRate)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    auto readBytes = [&file](std::size_t count) {
        std::vector<char> buf(count);
        file.read(buf.data(), static_cast<std::streamsize>(count));
        return buf;
    };
    auto toU32 = [](const char* p) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
    };
    auto toU16 = [](const char* p) {
        return static_cast<std::uint16_t>(static_cast<unsigned char>(p[0])) |
               (static_cast<std::uint16_t>(static_cast<unsigned char>(p[1])) << 8);
    };

    const std::vector<char> header = readBytes(12);
    if (header.size() < 12 || std::memcmp(header.data(), "RIFF", 4) != 0 ||
        std::memcmp(header.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    int sourceRate = 0;
    int channels = 0;
    int format = 0;
    int bitsPerSample = 0;
    std::vector<float> samplesL;
    std::vector<float> samplesR;

    while (file) {
        const std::vector<char> chunk = readBytes(8);
        if (chunk.size() < 8) {
            break;
        }
        const std::uint32_t chunkSize = toU32(chunk.data() + 4);
        if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
            const std::vector<char> fmt = readBytes(chunkSize < 16 ? chunkSize : 16);
            format = toU16(fmt.data());
            channels = toU16(fmt.data() + 2);
            sourceRate = static_cast<int>(toU32(fmt.data() + 4));
            bitsPerSample = toU16(fmt.data() + 14);
            if (chunkSize > 16) {
                file.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
            }
        } else if (std::memcmp(chunk.data(), "data", 4) == 0) {
            const std::vector<char> data = readBytes(chunkSize);
            const std::size_t frames = data.size() / (static_cast<std::size_t>(channels) * (bitsPerSample / 8));
            samplesL.resize(frames);
            samplesR.resize(frames);
            for (std::size_t f = 0; f < frames; ++f) {
                auto sampleAt = [&](int ch) {
                    const std::size_t offset = f * static_cast<std::size_t>(channels) * (bitsPerSample / 8) +
                                               static_cast<std::size_t>(ch) * (bitsPerSample / 8);
                    if (format == 3 && bitsPerSample == 32) {
                        float value;
                        std::memcpy(&value, data.data() + offset, 4);
                        return value;
                    }
                    if (format == 1 && bitsPerSample == 16) {
                        return static_cast<float>(static_cast<std::int16_t>(toU16(data.data() + offset))) / 32768.0f;
                    }
                    if (format == 1 && bitsPerSample == 24) {
                        const std::uint32_t raw =
                            static_cast<std::uint32_t>(static_cast<unsigned char>(data.data()[offset])) |
                            (static_cast<std::uint32_t>(static_cast<unsigned char>(data.data()[offset + 1])) << 8) |
                            (static_cast<std::uint32_t>(static_cast<unsigned char>(data.data()[offset + 2])) << 16);
                        const std::int32_t value = static_cast<std::int32_t>(raw << 8) >> 8;
                        return static_cast<float>(value) / 8388608.0f;
                    }
                    return 0.0f;
                };
                samplesL[f] = sampleAt(0);
                samplesR[f] = channels > 1 ? sampleAt(1) : sampleAt(0);
            }
            break;
        } else {
            file.seekg(static_cast<std::streamoff>(chunkSize + (chunkSize & 1)), std::ios::cur);
        }
    }

    if (sourceRate == 0 || samplesL.empty()) {
        return false;
    }
    if (sourceRate == targetRate) {
        input.left = std::move(samplesL);
        input.right = std::move(samplesR);
        return true;
    }

    // linear resample to the engine rate
    const double ratio = static_cast<double>(sourceRate) / static_cast<double>(targetRate);
    const std::size_t outFrames = static_cast<std::size_t>(static_cast<double>(samplesL.size()) / ratio);
    input.left.resize(outFrames);
    input.right.resize(outFrames);
    for (std::size_t i = 0; i < outFrames; ++i) {
        const double pos = static_cast<double>(i) * ratio;
        const std::size_t i0 = static_cast<std::size_t>(pos);
        const std::size_t i1 = i0 + 1 < samplesL.size() ? i0 + 1 : i0;
        const double frac = pos - static_cast<double>(i0);
        input.left[i] = static_cast<float>(samplesL[i0] * (1.0 - frac) + samplesL[i1] * frac);
        input.right[i] = static_cast<float>(samplesR[i0] * (1.0 - frac) + samplesR[i1] * frac);
    }
    return true;
}

void writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right,
              int sampleRate)
{
    const std::uint32_t dataSize = static_cast<std::uint32_t>(left.size() * kChannels * 2);
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate * kChannels * 2);
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
    writeU32(sampleRate);
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
    std::printf("usage: namfx_render --preset <file.json> [--in input.wav] [--seconds N] [--freq Hz] [--out out.wav] [--rate 44100|48000|96000]\n");
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    std::string presetPath;
    std::string inPath;
    std::string outPath = "render.wav";
    double seconds = 2.0;
    double freq = 440.0;
    int sampleRate = 48000;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--preset" && i + 1 < argc) {
            presetPath = argv[++i];
        } else if (arg == "--in" && i + 1 < argc) {
            inPath = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stod(argv[++i]);
        } else if (arg == "--freq" && i + 1 < argc) {
            freq = std::stod(argv[++i]);
        } else if (arg == "--rate" && i + 1 < argc) {
            sampleRate = std::stoi(argv[++i]);
        } else {
            return usage();
        }
    }
    if (presetPath.empty()) {
        return usage();
    }
    if (sampleRate != 44100 && sampleRate != 48000 && sampleRate != 96000) {
        std::printf("error: --rate must be 44100, 48000 or 96000\n");
        return 1;
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
    namfx::registerTs808(*registry);
    namfx::registerTransparent(*registry);
    namfx::registerMosfetOd(*registry);

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
    chain.prepare(sampleRate, 64);

    AudioInput input;
    if (!inPath.empty()) {
        if (!readWav(inPath, input, sampleRate)) {
            std::printf("error: cannot read wav %s (need 16/24-bit PCM or float32)\n", inPath.c_str());
            return 1;
        }
        seconds = static_cast<double>(input.left.size()) / sampleRate;
        std::printf("using %s: %d samples, %d sec\n", inPath.c_str(),
                    static_cast<int>(input.left.size()), static_cast<int>(seconds));
    }

    const int totalSamples = static_cast<int>(seconds * sampleRate);
    std::vector<float> inL(64, 0.0f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(static_cast<std::size_t>(totalSamples));
    std::vector<float> outR(static_cast<std::size_t>(totalSamples));
    double phase = 0.0;
    constexpr double kTwoPi = 6.28318530717958647692;
    for (int offset = 0; offset < totalSamples; offset += 64) {
        const int count = totalSamples - offset < 64 ? totalSamples - offset : 64;
        if (!inPath.empty()) {
            for (int i = 0; i < count; ++i) {
                const std::size_t idx = static_cast<std::size_t>(offset + i);
                inL[static_cast<std::size_t>(i)] = input.left[idx];
                inR[static_cast<std::size_t>(i)] = input.right[idx];
            }
        } else {
            for (int i = 0; i < count; ++i) {
                inL[static_cast<std::size_t>(i)] =
                    static_cast<float>(std::sin(phase) * 0.5);
                inR[static_cast<std::size_t>(i)] = 0.0f;
                phase += kTwoPi * freq / sampleRate;
            }
        }
        chain.process(inL.data(), inR.data(), outL.data() + offset, outR.data() + offset, count);
    }

    writeWav(outPath, outL, outR, sampleRate);
    std::printf("rendered %d samples to %s\n", totalSamples, outPath.c_str());
    return 0;
}
