#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace namfx {
namespace ir {

struct WavData {
    std::vector<float> samples; // mono (first channel when multi-channel)
    double sampleRate = 0.0;
    bool ok = false;
};

// Parse a RIFF/WAVE file from memory: PCM 16/24/32-bit and IEEE float32/64,
// mono or stereo (first channel is taken). Load-path only, never in the
// audio callback. Empty data or unsupported format yields ok = false.
WavData parseWav(const std::uint8_t* data, std::size_t size);

// Parse a WAV file from disk.
WavData loadWavFile(const std::string& path);

} // namespace ir
} // namespace namfx
