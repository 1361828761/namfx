#pragma once

#include <string>

namespace namfx {

enum class Taper {
    Linear,
    Log,
};

struct ParamSpec {
    std::string id;
    std::string displayName;
    float min = 0.0f;
    float max = 1.0f;
    float defaultValue = 0.0f;
    std::string unit;
    Taper taper = Taper::Linear;
};

} // namespace namfx
