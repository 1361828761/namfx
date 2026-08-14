#include "modules/module_registry.h"
#include "preset/export_import.h"
#include "test_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::filesystem::path tempDir(const char* tag)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("namfx_export_" + std::string(tag) + "_"
           + std::to_string(static_cast<unsigned long long>(
               std::chrono::steady_clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeBytes(const std::filesystem::path& path, const std::string& data)
{
    std::ofstream f(path, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

} // namespace

TEST_CASE("preset export packages assets and rewrites file paths")
{
    const std::filesystem::path dir = tempDir("a");
    const std::filesystem::path models = dir / "models";
    std::filesystem::create_directories(models);
    writeBytes(models / "amp.nam", "fake-nam-bytes");
    const std::filesystem::path exportDir = dir / "out";
    std::filesystem::create_directories(exportDir);

    namfx::preset::Preset preset;
    preset.name = "Exported";
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "amp";
    def.impl = "nam";
    def.moduleId = "amp.nam";
    def.file = "models/amp.nam";
    preset.chain.push_back(def);

    namfx::preset::ExportReport report;
    REQUIRE(namfx::preset::exportPreset(preset, dir.string(), exportDir, report));
    REQUIRE(report.ok);
    REQUIRE(report.errors.empty());
    REQUIRE(std::filesystem::exists(exportDir / "preset.json"));
    REQUIRE(std::filesystem::exists(exportDir / "assets" / "amp.nam"));
    // the exported JSON must reference the relative assets path
    {
        std::ifstream f(exportDir / "preset.json");
        const std::string json((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        REQUIRE(json.find("\"file\": \"assets/amp.nam\"") != std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("preset export reports missing assets")
{
    const std::filesystem::path dir = tempDir("b");
    const std::filesystem::path exportDir = dir / "out";
    std::filesystem::create_directories(exportDir);

    namfx::preset::Preset preset;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "amp";
    def.impl = "nam";
    def.moduleId = "amp.nam";
    def.file = "models/gone.nam";
    preset.chain.push_back(def);

    namfx::preset::ExportReport report;
    REQUIRE(namfx::preset::exportPreset(preset, dir.string(), exportDir, report));
    REQUIRE_FALSE(report.ok); // missing asset -> not fully ok
    REQUIRE(report.errors.size() == 1);
    REQUIRE(report.errors[0].find("gone.nam") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("preset import round trips an exported preset")
{
    const std::filesystem::path dir = tempDir("c");
    const std::filesystem::path models = dir / "models";
    std::filesystem::create_directories(models);
    writeBytes(models / "amp.nam", "fake-nam-bytes");
    const std::filesystem::path exportDir = dir / "out";
    std::filesystem::create_directories(exportDir);

    namfx::preset::Preset preset;
    preset.name = "RoundTrip";
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "amp";
    def.impl = "nam";
    def.moduleId = "amp.nam";
    def.file = "models/amp.nam";
    def.params.push_back(namfx::ParamInit{"gain", 0.3f});
    preset.chain.push_back(def);

    namfx::preset::ExportReport exportReport;
    REQUIRE(namfx::preset::exportPreset(preset, dir.string(), exportDir, exportReport));

    const auto registry = testx::makeRegistry();
    namfx::preset::ImportReport importReport;
    const namfx::preset::Preset imported =
        namfx::preset::importPreset(exportDir, 1, *registry, importReport);
    REQUIRE(importReport.ok);
    REQUIRE(importReport.missingAssets.empty());
    REQUIRE(imported.name == "RoundTrip");
    REQUIRE(imported.chain.size() == 1);
    // file is resolved against the package dir (load semantics): absolute,
    // pointing into the exported assets (path / string keeps separators)
    REQUIRE(imported.chain[0].file == (exportDir / "assets/amp.nam").string());
    REQUIRE(imported.chain[0].params.size() == 1);
    REQUIRE(imported.chain[0].params[0].value == 0.3f);
    std::filesystem::remove_all(dir);
}

TEST_CASE("preset import rejects newer schemas and reports missing assets")
{
    const std::filesystem::path dir = tempDir("d");
    const std::filesystem::path exportDir = dir / "out";
    std::filesystem::create_directories(exportDir);
    writeBytes(exportDir / "preset.json",
               R"({"schema": 2, "name": "Future", "chain": [{
                   "slot": 0, "category": "amp", "impl": "nam", "module": "amp.nam",
                   "file": "assets/missing.nam", "params": {}, "bypass": false, "mix": 1.0
               }], "scenes": []})");

    const auto registry = testx::makeRegistry();
    namfx::preset::ImportReport report;
    namfx::preset::importPreset(exportDir, 1, *registry, report);
    REQUIRE_FALSE(report.ok);
    REQUIRE(report.errors.size() == 1);
    REQUIRE(report.errors[0].find("schema") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("preset import reports missing assets while parsing the package")
{
    const std::filesystem::path dir = tempDir("e");
    const std::filesystem::path exportDir = dir / "out";
    std::filesystem::create_directories(exportDir);
    writeBytes(exportDir / "preset.json",
               R"({"schema": 1, "name": "NoAsset", "chain": [{
                   "slot": 0, "category": "amp", "impl": "nam", "module": "amp.nam",
                   "file": "assets/missing.nam", "params": {}, "bypass": false, "mix": 1.0
               }], "scenes": []})");

    const auto registry = testx::makeRegistry();
    namfx::preset::ImportReport report;
    namfx::preset::importPreset(exportDir, 1, *registry, report);
    REQUIRE(report.missingAssets.size() == 1);
    REQUIRE(report.missingAssets[0] == "assets/missing.nam");
    std::filesystem::remove_all(dir);
}
