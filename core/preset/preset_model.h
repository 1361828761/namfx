#pragma once

#include "audio/param_store.h"
#include "audio/slot.h"

#include <string>
#include <vector>

namespace namfx {
namespace preset {

struct SceneOverride {
    std::string moduleId;
    std::vector<ParamInit> params;
    bool bypass = false;
};

struct SceneDef {
    std::string name;
    std::vector<SceneOverride> overrides;
};

struct Preset {
    int schema = 1;
    std::string name;
    std::vector<audio::SlotDef> chain;
    std::vector<SceneDef> scenes;
};

bool operator==(const Preset& a, const Preset& b);
bool operator!=(const Preset& a, const Preset& b);
bool operator==(const SceneDef& a, const SceneDef& b);
bool operator==(const SceneOverride& a, const SceneOverride& b);

} // namespace preset
} // namespace namfx
