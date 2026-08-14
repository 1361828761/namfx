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
    std::string file; // asset path (IR wav / NAM model), PLAN sec 7 v1 field
    std::vector<ParamInit> params;
    bool bypass = false;
    float mix = 1.0f;
};

} // namespace audio
} // namespace namfx
