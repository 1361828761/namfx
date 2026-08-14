#include "preset/export_import.h"

#include "preset/preset_io.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace namfx {
namespace preset {

namespace {

bool readFileText(const std::filesystem::path& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

bool exportPreset(const Preset& preset, const std::string& baseDir,
                  const std::filesystem::path& destDir, ExportReport& report)
{
    report = ExportReport{};
    if (!std::filesystem::exists(destDir)) {
        report.errors.push_back("export directory does not exist");
        return false;
    }
    Preset exported = preset;
    const std::filesystem::path assetsDir = destDir / "assets";
    std::filesystem::create_directories(assetsDir);
    bool anyMissing = false;
    for (audio::SlotDef& def : exported.chain) {
        if (def.file.empty()) {
            continue;
        }
        const std::filesystem::path filePath(def.file);
        std::filesystem::path resolved = filePath.is_absolute()
            ? filePath
            : std::filesystem::path(baseDir) / filePath;
        if (!std::filesystem::exists(resolved)) {
            report.errors.push_back("missing asset: " + def.file);
            anyMissing = true;
            continue;
        }
        const std::string name = filePath.filename().string();
        const std::filesystem::path target = assetsDir / name;
        std::error_code ec;
        std::filesystem::copy_file(resolved, target,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            report.errors.push_back("copy failed for " + def.file + ": " + ec.message());
            anyMissing = true;
            continue;
        }
        // rewrite to the exported (relative) location; forward slashes so
        // the package is portable across desktop/embedded
        def.file = "assets/" + name;
    }
    const std::string json = savePreset(exported);
    std::ofstream out(destDir / "preset.json", std::ios::binary);
    if (!out) {
        report.errors.push_back("cannot write preset.json");
        return false;
    }
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    report.ok = !anyMissing;
    return true;
}

Preset importPreset(const std::filesystem::path& sourceDir, int maxSchema,
                    const ModuleRegistry& registry, ImportReport& report)
{
    report = ImportReport{};
    std::string json;
    if (!readFileText(sourceDir / "preset.json", json)) {
        report.errors.push_back("preset.json missing or unreadable");
        return Preset{};
    }
    // schema gate before full validation
    const std::string marker = "\"schema\":";
    const std::size_t pos = json.find(marker);
    int schema = 1;
    if (pos != std::string::npos) {
        schema = std::atoi(json.c_str() + pos + marker.size());
    }
    if (schema > maxSchema) {
        report.errors.push_back("schema " + std::to_string(schema) + " newer than supported "
                                + std::to_string(maxSchema));
        return Preset{};
    }
    // lightweight parse of file fields for the missing-asset report
    for (std::size_t at = json.find("\"file\""); at != std::string::npos;
         at = json.find("\"file\"", at + 1)) {
        const std::size_t colon = json.find(':', at);
        const std::size_t q1 = json.find('"', colon + 1);
        const std::size_t q2 = json.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) {
            continue;
        }
        const std::string file = json.substr(q1 + 1, q2 - q1 - 1);
        if (file.empty()) {
            continue;
        }
        if (!std::filesystem::exists(sourceDir / file)) {
            report.missingAssets.push_back(file);
        }
    }

    namfx::preset::LoadReport loadReport;
    Preset preset = loadPreset(json, LoadMode::Strict, registry, loadReport, sourceDir.string());
    if (!loadReport.ok()) {
        report.errors.insert(report.errors.end(), loadReport.errors.begin(),
                             loadReport.errors.end());
        return Preset{};
    }
    report.ok = report.errors.empty();
    return preset;
}

} // namespace preset
} // namespace namfx
