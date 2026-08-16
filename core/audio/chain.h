#pragma once

#include "audio/slot.h"
#include "modules/param_spec.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace namfx {

class ModuleBase;
class ModuleRegistry;
class ParamStore;

namespace audio {

// std::atomic is not copy/move constructible; this wrapper keeps the
// per-slot mix override movable so SlotRuntime stays vector-friendly
struct AtomicMix {
    std::atomic<float> v{-1.0f};
    AtomicMix() = default;
    AtomicMix(const AtomicMix& o) : v(o.v.load(std::memory_order_relaxed)) {}
    AtomicMix& operator=(const AtomicMix& o)
    {
        v.store(o.v.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }
    AtomicMix(AtomicMix&& o) noexcept : v(o.v.load(std::memory_order_relaxed)) {}
    AtomicMix& operator=(AtomicMix&& o) noexcept
    {
        v.store(o.v.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }
};

class Chain {
public:
    // assetLoader (optional): custom asset resolution hook used instead of
    // ModuleBase::loadAsset(path) when non-null (browser/WASM in-memory
    // assets). Called on the load path, never from the audio callback.
    using AssetLoader = std::function<bool(ModuleBase& module, const SlotDef& slot)>;

    Chain(std::vector<SlotDef> slots, std::shared_ptr<const ModuleRegistry> registry,
          int maxSlots = 8, AssetLoader assetLoader = nullptr);

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

    // UI mix control: atomic per-slot wet/dry override (any thread); the
    // audio thread reads it every block. -1 means "use the preset mix".
    void setMixByIndex(int slotIndex, float mix);
    float mixValueOf(int slotIndex) const; // current effective mix (0..1)

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
        AtomicMix mixVal; // -1 = use def.mix
    };

    int fadeSamples(const std::string& impl) const;
    void startFade(SlotRuntime& runtime, bool toWet);

    std::vector<SlotRuntime> slots_;
    std::shared_ptr<const ModuleRegistry> registry_;
    AssetLoader assetLoader_;
    int maxSlots_ = kMinSlots;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
};

} // namespace audio
} // namespace namfx
