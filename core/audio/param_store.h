#pragma once

#include "modules/param_spec.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace namfx {

struct ParamInit {
    std::string id;
    float value = 0.0f;
};

class ParamStore {
public:
    explicit ParamStore(std::vector<ParamSpec> specs);

    void setSampleRate(double sampleRate);

    void set(const std::string& id, float value);
    void setImmediate(const std::string& id, float value);
    float get(const std::string& id) const;
    float target(const std::string& id) const;

    void advance(int n);
    bool isRamping() const;

    static constexpr double kMaxSmoothingMs = 10.0;

private:
    struct Slot {
        ParamSpec spec;
        float value = 0.0f;
        float target = 0.0f;
        float step = 0.0f;
        int remaining = 0;
    };

    const Slot& slotFor(const std::string& id) const;
    Slot& slotFor(const std::string& id);
    void startRamp(Slot& slot);
    static float clampTo(const ParamSpec& spec, float value);

    std::vector<Slot> slots_;
    std::unordered_map<std::string, std::size_t> index_;
    double sampleRate_ = 48000.0;
};

} // namespace namfx
