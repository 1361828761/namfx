#include "modules/ir/wav_io.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace namfx {
namespace ir {

namespace {

bool readU32(const std::uint8_t* p, std::uint32_t& out)
{
    out = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    return true;
}

bool readU16(const std::uint8_t* p, std::uint16_t& out)
{
    out = static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
    return true;
}

} // namespace

WavData parseWav(const std::uint8_t* data, std::size_t size)
{
    WavData result;
    if (data == nullptr || size < 44) {
        return result;
    }
    // RIFF header
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        return result;
    }

    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    bool haveFmt = false;

    std::size_t offset = 12;
    while (offset + 8 <= size) {
        const std::uint32_t chunkId = static_cast<std::uint32_t>(data[offset])
            | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
            | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
            | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
        std::uint32_t chunkSize = 0;
        readU32(data + offset + 4, chunkSize);
        const std::size_t body = offset + 8;
        if (body + chunkSize > size) {
            return result; // truncated chunk
        }
        if (chunkId == 0x20746D66u) { // "fmt "
            if (chunkSize >= 16) {
                readU16(data + body, audioFormat);
                readU16(data + body + 2, channels);
                readU32(data + body + 4, sampleRate);
                readU16(data + body + 14, bitsPerSample);
                haveFmt = true;
            }
        } else if (chunkId == 0x61746164u) { // "data"
            if (!haveFmt || (audioFormat != 1 && audioFormat != 3) || channels == 0
                || channels > 2 || sampleRate == 0) {
                return result;
            }
            const bool isFloat = audioFormat == 3;
            const std::size_t bytesPerSample = bitsPerSample / 8;
            if (bytesPerSample == 0 || (!isFloat && bitsPerSample != 16 && bitsPerSample != 24
                                        && bitsPerSample != 32)
                || (isFloat && bitsPerSample != 32 && bitsPerSample != 64)) {
                return result;
            }
            const std::size_t frameBytes = bytesPerSample * channels;
            const std::size_t frameCount = chunkSize / frameBytes;
            result.samples.resize(frameCount);
            for (std::size_t f = 0; f < frameCount; ++f) {
                const std::uint8_t* p = data + body + f * frameBytes;
                float value = 0.0f;
                if (isFloat && bitsPerSample == 32) {
                    std::memcpy(&value, p, 4);
                } else if (isFloat && bitsPerSample == 64) {
                    double d = 0.0;
                    std::memcpy(&d, p, 8);
                    value = static_cast<float>(d);
                } else if (bitsPerSample == 16) {
                    std::int16_t s = 0;
                    std::memcpy(&s, p, 2);
                    value = static_cast<float>(s) / 32768.0f;
                } else if (bitsPerSample == 24) {
                    const std::int32_t s = static_cast<std::int32_t>(
                        static_cast<std::int32_t>(p[0]) | (static_cast<std::int32_t>(p[1]) << 8)
                        | (static_cast<std::int32_t>(p[2]) << 16));
                    const std::int32_t shifted = (s << 8) >> 8; // sign extend
                    value = static_cast<float>(shifted) / 8388608.0f;
                } else { // 32-bit PCM
                    std::int32_t s = 0;
                    std::memcpy(&s, p, 4);
                    value = static_cast<float>(s) / 2147483648.0f;
                }
                result.samples[f] = value;
            }
            result.sampleRate = sampleRate;
            result.ok = true;
            return result;
        }
        offset = body + chunkSize + (chunkSize & 1u); // chunks are word aligned
    }
    return result;
}

WavData loadWavFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return WavData{};
    }
    stream.seekg(0, std::ios::end);
    const std::streampos endPos = stream.tellg();
    if (endPos <= 0) {
        return WavData{};
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(endPos));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        return WavData{};
    }
    return parseWav(bytes.data(), bytes.size());
}

} // namespace ir
} // namespace namfx
