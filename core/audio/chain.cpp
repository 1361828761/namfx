#include "audio/chain.h"

#include "audio/param_store.h"
#include "modules/module_base.h"
#include "modules/module_registry.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace namfx {
namespace audio {

namespace {

int msToSamples(double sampleRate, double ms)
{
    const double total = sampleRate * ms / 1000.0;
    if (total < 1.0) {
        return 1;
    }
    return static_cast<int>(total);
}

} // namespace

Chain::Chain(std::vector<SlotDef> slots, std::shared_ptr<const ModuleRegistry> registry, int maxSlots)
    : registry_(std::move(registry))
    , maxSlots_(std::max(maxSlots, kMinSlots))
{
    if (!registry_) {
        throw std::runtime_error("chain: null module registry");
    }
    std::sort(slots.begin(), slots.end(),
              [](const SlotDef& a, const SlotDef& b) { return a.slot < b.slot; });
    slots_.reserve(slots.size());
    int previous = -1;
    for (SlotDef& def : slots) {
        if (def.slot < 0 || def.slot >= maxSlots_) {
            throw std::runtime_error("chain: slot index out of range: " + std::to_string(def.slot));
        }
        if (def.slot == previous) {
            throw std::runtime_error("chain: duplicate slot index: " + std::to_string(def.slot));
        }
        previous = def.slot;
        if (!registry_->has(def.moduleId)) {
            throw std::runtime_error("chain: unknown module id: " + def.moduleId);
        }
        SlotRuntime runtime;
        runtime.def = std::move(def);
        runtime.specs = registry_->specsFor(runtime.def.moduleId);
        runtime.module = registry_->create(runtime.def.moduleId);
        if (!runtime.def.file.empty() && !runtime.module->loadAsset(runtime.def.file)) {
            throw std::runtime_error("chain: failed to load asset '" + runtime.def.file
                                     + "' for module '" + runtime.def.moduleId + "'");
        }
        runtime.store = std::make_unique<ParamStore>(runtime.specs);
        for (const ParamInit& init : runtime.def.params) {
            const ParamSpec* spec = registry_->findParam(runtime.def.moduleId, init.id);
            if (spec == nullptr) {
                throw std::runtime_error("chain: unknown param '" + init.id + "' for module '"
                                         + runtime.def.moduleId + "'");
            }
            runtime.store->setImmediate(init.id, init.value);
        }
        runtime.fade = runtime.def.bypass ? 0.0f : 1.0f;
        runtime.fadeTarget = runtime.fade;
        slots_.push_back(std::move(runtime));
    }
}

Chain::~Chain() = default;
Chain::Chain(Chain&&) noexcept = default;
Chain& Chain::operator=(Chain&&) noexcept = default;

int Chain::slotCount() const
{
    return static_cast<int>(slots_.size());
}

int Chain::fadeSamples(const std::string& impl) const
{
    if (impl == "ir") {
        return msToSamples(sampleRate_, 5.0);
    }
    return msToSamples(sampleRate_, 1.0);
}

void Chain::startFade(SlotRuntime& runtime, bool toWet)
{
    const float target = toWet ? 1.0f : 0.0f;
    if (runtime.fade == target) {
        runtime.fadeRemaining = 0;
        runtime.fadeStep = 0.0f;
        runtime.fadeTarget = target;
        return;
    }
    runtime.fadeTarget = target;
    runtime.fadeRemaining = fadeSamples(runtime.def.impl);
    runtime.fadeStep = (target - runtime.fade) / static_cast<float>(runtime.fadeRemaining);
}

void Chain::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(maxBlockSize, 1);
    for (SlotRuntime& runtime : slots_) {
        runtime.dryL.resize(static_cast<std::size_t>(maxBlock_));
        runtime.dryR.resize(static_cast<std::size_t>(maxBlock_));
        runtime.wetL.resize(static_cast<std::size_t>(maxBlock_));
        runtime.wetR.resize(static_cast<std::size_t>(maxBlock_));
        runtime.module->prepare(sampleRate, maxBlock_);
        runtime.module->setSampleRate(sampleRate);
        runtime.module->setMaxBlock(maxBlock_);
        runtime.store->setSampleRate(sampleRate);
        // push current parameter values so asset options (e.g. NAM tier)
        // see their targets before the first audio callback
        for (const ParamSpec& spec : runtime.specs) {
            runtime.module->setParameter(spec.id, runtime.store->get(spec.id));
        }
        runtime.module->applyAssetOptions();
    }
}

void Chain::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    assert(n >= 0);
    assert(n <= maxBlock_);
    std::memcpy(outL, inL, static_cast<std::size_t>(n) * sizeof(float));
    std::memcpy(outR, inR, static_cast<std::size_t>(n) * sizeof(float));
    for (SlotRuntime& runtime : slots_) {
        runtime.store->advance(n);
        for (const ParamSpec& spec : runtime.specs) {
            runtime.module->setParameter(spec.id, runtime.store->get(spec.id));
        }
        std::memcpy(runtime.dryL.data(), outL, static_cast<std::size_t>(n) * sizeof(float));
        std::memcpy(runtime.dryR.data(), outR, static_cast<std::size_t>(n) * sizeof(float));
        float* wetL = runtime.wetL.data();
        float* wetR = runtime.wetR.data();
        switch (runtime.module->channelMode()) {
        case ChannelMode::MonoInMonoOut:
            runtime.module->process(runtime.dryL.data(), runtime.dryR.data(), wetL, wetR, n);
            wetR = wetL;
            break;
        case ChannelMode::MonoInStereoOut:
            runtime.module->process(runtime.dryL.data(), runtime.dryR.data(), wetL, wetR, n);
            break;
        case ChannelMode::StereoInStereoOut:
            runtime.module->process(runtime.dryL.data(), runtime.dryR.data(), wetL, wetR, n);
            break;
        }
        const float mix = runtime.def.mix;
        const float fade = runtime.fade;
        if (runtime.fadeRemaining == 0 && fade == 1.0f && mix == 1.0f) {
            std::memcpy(outL, wetL, static_cast<std::size_t>(n) * sizeof(float));
            std::memcpy(outR, wetR, static_cast<std::size_t>(n) * sizeof(float));
            continue;
        }
        if (runtime.fadeRemaining == 0 && fade * mix == 0.0f) {
            continue;
        }
        float fading = fade;
        for (int i = 0; i < n; ++i) {
            const float wetness = fading * mix;
            outL[i] = runtime.dryL[i] + (wetL[i] - runtime.dryL[i]) * wetness;
            outR[i] = runtime.dryR[i] + (wetR[i] - runtime.dryR[i]) * wetness;
            if (runtime.fadeRemaining > 0) {
                fading += runtime.fadeStep;
                --runtime.fadeRemaining;
                if (runtime.fadeRemaining == 0) {
                    fading = runtime.fadeTarget;
                }
            }
        }
        runtime.fade = fading;
    }
}

void Chain::reset()
{
    for (SlotRuntime& runtime : slots_) {
        runtime.module->reset();
        runtime.fade = runtime.def.bypass ? 0.0f : 1.0f;
        runtime.fadeTarget = runtime.fade;
        runtime.fadeRemaining = 0;
        runtime.fadeStep = 0.0f;
    }
}

void Chain::setBypass(int slotIndex, bool bypass)
{
    for (SlotRuntime& runtime : slots_) {
        if (runtime.def.slot == slotIndex) {
            runtime.def.bypass = bypass;
            startFade(runtime, !bypass);
            return;
        }
    }
    throw std::out_of_range("chain: no slot with index " + std::to_string(slotIndex));
}

int Chain::slotIndexOf(const std::string& moduleId) const
{
    for (const SlotRuntime& runtime : slots_) {
        if (runtime.def.moduleId == moduleId) {
            return runtime.def.slot;
        }
    }
    return -1;
}

std::size_t Chain::paramIndexOf(int slotIndex, const std::string& paramId) const
{
    for (const SlotRuntime& runtime : slots_) {
        if (runtime.def.slot == slotIndex) {
            return runtime.store->indexOf(paramId);
        }
    }
    throw std::out_of_range("chain: no slot with index " + std::to_string(slotIndex));
}

void Chain::setParamByIndex(int slotIndex, std::size_t paramIndex, float value)
{
    for (SlotRuntime& runtime : slots_) {
        if (runtime.def.slot == slotIndex) {
            runtime.store->setByIndex(paramIndex, value);
            return;
        }
    }
}

void Chain::setBypassByIndex(int slotIndex, bool bypass)
{
    for (SlotRuntime& runtime : slots_) {
        if (runtime.def.slot == slotIndex) {
            runtime.def.bypass = bypass;
            startFade(runtime, !bypass);
            return;
        }
    }
}

} // namespace audio
} // namespace namfx
