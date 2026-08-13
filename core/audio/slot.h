#pragma once

#include "audio/param_store.h"

#include <string>
#include <vector>

namespace namfx {
namespace audio {

struct SlotDef {
    int slot = 0;
    std::string category;
    std::string impl;
    std::string moduleId;
    std::vector<ParamInit> params;
    bool bypass = false;
    float mix = 1.0f;
};

} // namespace audio
} // namespace namfx
