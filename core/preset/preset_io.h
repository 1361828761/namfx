#pragma once

#include "modules/module_registry.h"
#include "preset/migration.h"
#include "preset/preset_model.h"

#include <string>
#include <vector>

namespace namfx {
namespace preset {

constexpr int kMaxScenes = 8;

enum class LoadMode {
    Tolerant,
    Strict,
};

struct LoadReport {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    bool ok() const { return errors.empty(); }
};

Preset loadPreset(const std::string& jsonText, LoadMode mode, const ModuleRegistry& registry,
                  LoadReport& report);

std::string savePreset(const Preset& preset);

} // namespace preset
} // namespace namfx
