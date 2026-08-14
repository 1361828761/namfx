#pragma once

// Test helper: build WAV file bytes in memory for the IR tests.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace testx {

struct WavFixture {
    // format: 1 = PCM, 3 = IEEE float; bits: 16/24/32 (PCM) or 32/64 (float);
    // samples are interleaved over channels (first channel is used by the IR loader)
    static std::vector<std::uint8_t> makeWav(int format, int bits, int channels, int sampleRate,
                                             const std::vector<float>& samples)
    {
        const int bytesPerSample = bits / 8;
        const std::size_t frameCount = samples.size() / static_cast<std::size_t>(channels);
        const std::size_t dataSize = frameCount * static_cast<std::size_t>(bytesPerSample * channels);
        std::vector<std::uint8_t> out(44 + dataSize, 0);

        std::memcpy(out.data(), "RIFF", 4);
        const std::uint32_t riffSize = static_cast<std::uint32_t>(36 + dataSize);
        std::memcpy(out.data() + 4, &riffSize, 4);
        std::memcpy(out.data() + 8, "WAVE", 4);
        std::memcpy(out.data() + 12, "fmt ", 4);
        const std::uint32_t fmtSize = 16;
        std::memcpy(out.data() + 16, &fmtSize, 4);
        const std::uint16_t fmt = static_cast<std::uint16_t>(format);
        std::memcpy(out.data() + 20, &fmt, 2);
        const std::uint16_t ch = static_cast<std::uint16_t>(channels);
        std::memcpy(out.data() + 22, &ch, 2);
        const std::uint32_t rate = static_cast<std::uint32_t>(sampleRate);
        std::memcpy(out.data() + 24, &rate, 4);
        const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate) * bytesPerSample * channels;
        std::memcpy(out.data() + 28, &byteRate, 4);
        const std::uint16_t blockAlign = static_cast<std::uint16_t>(bytesPerSample * channels);
        std::memcpy(out.data() + 32, &blockAlign, 2);
        const std::uint16_t bitsOut = static_cast<std::uint16_t>(bits);
        std::memcpy(out.data() + 34, &bitsOut, 2);
        std::memcpy(out.data() + 36, "data", 4);
        const std::uint32_t dataSize32 = static_cast<std::uint32_t>(dataSize);
        std::memcpy(out.data() + 40, &dataSize32, 4);

        std::uint8_t* dst = out.data() + 44;
        for (std::size_t f = 0; f < frameCount; ++f) {
            for (int c = 0; c < channels; ++c) {
                const float value = samples[f * static_cast<std::size_t>(channels)
                                            + static_cast<std::size_t>(c)];
                if (format == 1 && bits == 16) {
                    const std::int16_t s = static_cast<std::int16_t>(value * 32767.0f);
                    std::memcpy(dst, &s, 2);
                } else if (format == 1 && bits == 24) {
                    const std::int32_t s = static_cast<std::int32_t>(value * 8388607.0f);
                    dst[0] = static_cast<std::uint8_t>(s & 0xFF);
                    dst[1] = static_cast<std::uint8_t>((s >> 8) & 0xFF);
                    dst[2] = static_cast<std::uint8_t>((s >> 16) & 0xFF);
                } else if (format == 1 && bits == 32) {
                    const std::int32_t s = static_cast<std::int32_t>(value * 2147483647.0f);
                    std::memcpy(dst, &s, 4);
                } else if (format == 3 && bits == 32) {
                    std::memcpy(dst, &value, 4);
                } else if (format == 3 && bits == 64) {
                    const double d = static_cast<double>(value);
                    std::memcpy(dst, &d, 8);
                }
                dst += bytesPerSample;
            }
        }
        return out;
    }

    static bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        if (!stream) {
            return false;
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(stream);
    }
};

} // namespace testx
