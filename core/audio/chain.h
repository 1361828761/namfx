#pragma once

#include "audio/slot.h"
#include "modules/param_spec.h"

#include <memory>
#include <vector>

namespace namfx {

class ModuleBase;
class ModuleRegistry;
class ParamStore;

namespace audio {

class Chain {
public:
    Chain(std::vector<SlotDef> slots, std::shared_ptr<const ModuleRegistry> registry,
          int maxSlots = 8);

    ~Chain();
    Chain(Chain&&) noexcept;
    Chain& operator=(Chain&&) noexcept;
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;

    void prepare(double sampleRate, int maxBlockSize);
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);
    void reset();

    void setBypass(int slotIndex, bool bypass);
    int slotCount() const;
    int maxSlots() const { return maxSlots_; }

    // control-thread lookup helpers for the scene engine (pre-resolve ids
    // to indices so the audio-thread application path never touches strings)
    int slotIndexOf(const std::string& moduleId) const;
    const std::string& moduleIdOf(int slotIndex) const;
    std::size_t paramIndexOf(int slotIndex, const std::string& paramId) const;
    const std::vector<ParamSpec>& specsOf(int slotIndex) const;

    // control-thread readout for the UI (chain summary): current (ramped)
    // value of a slot's parameter by index; throws std::out_of_range
    float paramValue(int slotIndex, std::size_t paramIndex) const;

    // control-thread copy of a slot's definition (module id, bypass, mix);
    // throws std::out_of_range
    SlotDef defOf(int slotIndex) const;

    // graph-swap protocol: call on the incoming chain right before
    // requestSwap so the swap eases in from dry instead of popping
    void startFadeIn();

    // audio-thread scene application (block boundary): index-based, zero
    // allocation, no string work
    void setParamByIndex(int slotIndex, std::size_t paramIndex, float value);
    void setBypassByIndex(int slotIndex, bool bypass);

    static constexpr int kMinSlots = 8;

private:
    struct SlotRuntime {
        SlotDef def;
        std::unique_ptr<ModuleBase> module;
        std::unique_ptr<ParamStore> store;
        std::vector<ParamSpec> specs;
        std::vector<float> dryL;
        std::vector<float> dryR;
        std::vector<float> wetL;
        std::vector<float> wetR;
        float fade = 1.0f;
        float fadeTarget = 1.0f;
        float fadeStep = 0.0f;
        int fadeRemaining = 0;
    };

    int fadeSamples(const std::string& impl) const;
    void startFade(SlotRuntime& runtime, bool toWet);

    std::vector<SlotRuntime> slots_;
    std::shared_ptr<const ModuleRegistry> registry_;
    int maxSlots_ = kMinSlots;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
};

} // namespace audio
} // namespace namfx
