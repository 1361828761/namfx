#pragma once

#include "preset/preset_model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace namfx {
class ModuleRegistry;
namespace preset {

// Preset export/import (PLAN M5a): a preset is exported as a self-contained
// directory `presets/<name>/` holding `preset.json` plus the referenced
// assets (.nam / IR wav) under `assets/`. Asset paths in the exported JSON
// are rewritten to be relative to the export directory, so the package can
// be copied anywhere and loaded with that directory as baseDir (double-end
// consistent: desktop and embedded use the same layout).
//
// Export resolves `file` fields against the preset's current baseDir;
// missing assets are reported (export continues, the preset stays valid but
// the file field keeps its absolute path).
struct ExportReport {
    bool ok = false;
    std::vector<std::string> errors;
};

// destDir must exist; writes destDir/preset.json and destDir/assets/*
bool exportPreset(const Preset& preset, const std::string& baseDir,
                  const std::filesystem::path& destDir, ExportReport& report);

struct ImportReport {
    bool ok = false; // schema/JSON/structure valid (missing assets are reported separately)
    std::vector<std::string> errors;
    std::vector<std::string> missingAssets;
};

// sourceDir must contain preset.json; validates schema against maxSchema
// (higher versions are rejected, per PLAN import rules) and reports missing
// assets item by item. On success the returned Preset's file fields are
// relative to sourceDir (load with baseDir = sourceDir). The registry is
// used for strict module validation.
Preset importPreset(const std::filesystem::path& sourceDir, int maxSchema,
                    const namfx::ModuleRegistry& registry, ImportReport& report);

} // namespace preset
} // namespace namfx
