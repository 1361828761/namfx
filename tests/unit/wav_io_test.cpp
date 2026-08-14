#include "modules/ir/wav_io.h"
#include "wav_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::vector<float> ramp(std::size_t n)
{
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<float>(i % 7) / 6.0f - 0.5f;
    }
    return out;
}

} // namespace

TEST_CASE("wav parser reads 16-bit PCM")
{
    const std::vector<float> samples = ramp(16);
    const auto bytes = testx::WavFixture::makeWav(1, 16, 1, 48000, samples);
    const namfx::ir::WavData wav = namfx::ir::parseWav(bytes.data(), bytes.size());

    REQUIRE(wav.ok);
    REQUIRE(wav.sampleRate == 48000.0);
    REQUIRE(wav.samples.size() == 16);
    for (std::size_t i = 0; i < 16; ++i) {
        REQUIRE(std::fabs(wav.samples[i] - samples[i]) < 1e-4);
    }
}

TEST_CASE("wav parser reads 24-bit and 32-bit PCM")
{
    for (int bits : {24, 32}) {
        const std::vector<float> samples = ramp(9);
        const auto bytes = testx::WavFixture::makeWav(1, bits, 1, 44100, samples);
        const namfx::ir::WavData wav = namfx::ir::parseWav(bytes.data(), bytes.size());
        REQUIRE(wav.ok);
        REQUIRE(wav.sampleRate == 44100.0);
        REQUIRE(wav.samples.size() == 9);
        for (std::size_t i = 0; i < 9; ++i) {
            REQUIRE(std::fabs(wav.samples[i] - samples[i]) < 1e-4);
        }
    }
}

TEST_CASE("wav parser reads IEEE float 32 and 64")
{
    const std::vector<float> samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
    for (int bits : {32, 64}) {
        const auto bytes = testx::WavFixture::makeWav(3, bits, 1, 96000, samples);
        const namfx::ir::WavData wav = namfx::ir::parseWav(bytes.data(), bytes.size());
        REQUIRE(wav.ok);
        REQUIRE(wav.sampleRate == 96000.0);
        REQUIRE(wav.samples.size() == 5);
        for (std::size_t i = 0; i < 5; ++i) {
            REQUIRE(wav.samples[i] == samples[i]);
        }
    }
}

TEST_CASE("wav parser takes the first channel of a stereo file")
{
    const std::vector<float> interleaved = {0.25f, -0.5f, 0.75f, -1.0f, 1.0f, 0.0f};
    const auto bytes = testx::WavFixture::makeWav(1, 16, 2, 48000, interleaved);
    const namfx::ir::WavData wav = namfx::ir::parseWav(bytes.data(), bytes.size());

    REQUIRE(wav.ok);
    REQUIRE(wav.samples.size() == 3);
    REQUIRE(std::fabs(wav.samples[0] - 0.25f) < 1e-4);
    REQUIRE(std::fabs(wav.samples[1] - 0.75f) < 1e-4);
    REQUIRE(std::fabs(wav.samples[2] - 1.0f) < 1e-4);
}

TEST_CASE("wav parser rejects truncated and malformed input")
{
    const std::vector<float> samples = ramp(8);
    const auto bytes = testx::WavFixture::makeWav(1, 16, 1, 48000, samples);

    // truncated mid-data
    const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + 30);
    REQUIRE_FALSE(namfx::ir::parseWav(cut.data(), cut.size()).ok);
    // not a RIFF file
    const std::vector<std::uint8_t> garbage(100, 0xAB);
    REQUIRE_FALSE(namfx::ir::parseWav(garbage.data(), garbage.size()).ok);
    // empty
    REQUIRE_FALSE(namfx::ir::parseWav(nullptr, 0).ok);
}

TEST_CASE("wav file loading round trips through disk")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("namfx_wav_" + std::to_string(static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "ir.wav";

    const std::vector<float> samples = {0.5f, -0.25f, 0.125f};
    REQUIRE(testx::WavFixture::writeFile(file.string(),
                                         testx::WavFixture::makeWav(1, 16, 1, 48000, samples)));
    const namfx::ir::WavData wav = namfx::ir::loadWavFile(file.string());
    REQUIRE(wav.ok);
    REQUIRE(wav.samples.size() == 3);
    REQUIRE(std::fabs(wav.samples[0] - 0.5f) < 1e-4);

    REQUIRE_FALSE(namfx::ir::loadWavFile((dir / "missing.wav").string()).ok);
    std::filesystem::remove_all(dir);
}
