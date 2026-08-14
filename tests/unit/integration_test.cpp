#include "audio/chain.h"
#include "modules/dsp/gain.h"
#include "modules/dsp/tone.h"
#include "preset/atomic_write.h"
#include "preset/backup.h"
#include "preset/preset_io.h"
#include "test_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef NAMFX_DEMO_DIR
#define NAMFX_DEMO_DIR ""
#endif

namespace {

std::filesystem::path tempDir()
{
    std::random_device rd;
    const std::filesystem::path base = std::filesystem::temp_directory_path()
        / ("namfx_test_" + std::to_string(rd()));
    std::filesystem::create_directories(base);
    return base;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::vector<std::filesystem::path> demoPresets()
{
    std::vector<std::filesystem::path> result;
    const std::filesystem::path dir = NAMFX_DEMO_DIR;
    if (dir.empty() || !std::filesystem::exists(dir)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            result.push_back(entry.path());
        }
    }
    return result;
}

} // namespace

TEST_CASE("every demo preset loads strictly and produces non silent output")
{
    const std::vector<std::filesystem::path> presets = demoPresets();
    REQUIRE_FALSE(presets.empty());

    auto registry = testx::makeRegistry();
    for (const std::filesystem::path& path : presets) {
        INFO("preset " << path.filename().string());
        namfx::preset::LoadReport report;
        const namfx::preset::Preset preset = namfx::preset::loadPreset(
            readFile(path), namfx::preset::LoadMode::Strict, *registry, report,
            path.parent_path().string());
        REQUIRE(report.ok());
        REQUIRE_FALSE(preset.chain.empty());

        namfx::audio::Chain chain(preset.chain, registry);
        constexpr int n = 2000;
        chain.prepare(48000.0, n);

        std::vector<float> inL(static_cast<std::size_t>(n));
        std::vector<float> inR(static_cast<std::size_t>(n), 0.0f);
        std::vector<float> outL(static_cast<std::size_t>(n));
        std::vector<float> outR(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            inL[static_cast<std::size_t>(i)] = static_cast<float>(
                0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / 48000.0));
        }
        chain.process(inL.data(), inR.data(), outL.data(), outR.data(), n);

        double energy = 0.0;
        for (float s : outL) {
            energy += static_cast<double>(s) * static_cast<double>(s);
        }
        REQUIRE(energy > 0.0);
    }
}

TEST_CASE("backup keeps the latest five versions and rotates the oldest out")
{
    const std::filesystem::path dir = tempDir();
    namfx::preset::BackupManager manager(dir / "backups", 5);

    for (int i = 0; i < 7; ++i) {
        REQUIRE(manager.save("MyPreset", "{\"version\":\"" + std::to_string(i) + "\"}"));
    }
    const std::vector<int> versions = manager.versions("MyPreset");
    REQUIRE(versions.size() == 5);
    REQUIRE(versions.front() == 3);
    REQUIRE(versions.back() == 7);

    std::string restored;
    REQUIRE(manager.restore("MyPreset", 7, restored));
    REQUIRE(restored.find("\"6\"") != std::string::npos);
    REQUIRE_FALSE(manager.restore("MyPreset", 1, restored));

    std::filesystem::remove_all(dir);
}

TEST_CASE("backup restore rejects corrupted files")
{
    const std::filesystem::path dir = tempDir();
    namfx::preset::BackupManager manager(dir / "backups", 5);
    REQUIRE(manager.save("P", "{\"ok\":true}"));

    const std::filesystem::path first = dir / "backups" / "P" / "v1.json";
    std::ofstream corrupt(first, std::ios::binary | std::ios::trunc);
    corrupt << "{ truncated";
    corrupt.close();

    std::string restored;
    REQUIRE_FALSE(manager.restore("P", 1, restored));
    std::filesystem::remove_all(dir);
}

TEST_CASE("preset saved to disk round trips through the file system")
{
    const std::filesystem::path dir = tempDir();
    auto registry = testx::makeRegistry();

    namfx::preset::Preset preset;
    preset.schema = 1;
    preset.name = "FileRoundTrip";
    namfx::audio::SlotDef slot;
    slot.slot = 0;
    slot.category = "pedal";
    slot.impl = "dsp";
    slot.moduleId = "gain";
    slot.params.push_back(namfx::ParamInit{"gain", 9.0f});
    preset.chain.push_back(std::move(slot));

    const std::filesystem::path file = dir / "presets" / "file_roundtrip.json";
    REQUIRE(namfx::preset::writeAtomically(file, namfx::preset::savePreset(preset)));

    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        readFile(file), namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE(report.ok());
    REQUIRE(loaded == preset);
    std::filesystem::remove_all(dir);
}
