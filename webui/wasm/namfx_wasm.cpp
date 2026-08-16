#include "namfx_wasm.h"

#include "audio/chain.h"
#include "audio/output_stage.h"
#include "modules/module_registry.h"
#include "modules/dsp/chorus.h"
#include "modules/dsp/dm2_delay.h"
#include "modules/dsp/flanger.h"
#include "modules/dsp/gain.h"
#include "modules/dsp/ge7_eq.h"
#include "modules/dsp/hall_reverb.h"
#include "modules/dsp/klon.h"
#include "modules/dsp/ns2_gate.h"
#include "modules/dsp/octave.h"
#include "modules/dsp/ocd.h"
#include "modules/dsp/ota_comp.h"
#include "modules/dsp/phaser.h"
#include "modules/dsp/pitch_shifter.h"
#include "modules/dsp/spring_reverb.h"
#include "modules/dsp/tape_delay.h"
#include "modules/dsp/tone.h"
#include "modules/dsp/ts808.h"
#include "modules/dsp/wah.h"
#include "modules/ir/cab_ir.h"
#include "modules/nam/nam_amp.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define NAMFX_WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define NAMFX_WASM_EXPORT
#endif

namespace {

using nlohmann::json;
using namfx::ModuleRegistry;
using namfx::ParamSpec;
using namfx::audio::Chain;
using namfx::audio::OutputStage;
using namfx::audio::SlotDef;

struct WasmEngine {
    std::shared_ptr<ModuleRegistry> registry = std::make_shared<ModuleRegistry>();
    std::unique_ptr<Chain> chain;
    OutputStage output;
    std::vector<float> chain_l;
    std::vector<float> chain_r;
    double sample_rate = 48000.0;
    int max_block_size = 2048;
    float input_level = 0.0f;
    float output_level = 0.0f;
    bool master_bypass = false;
    std::map<std::string, std::vector<std::uint8_t>> assets;
    std::string error;

    WasmEngine()
    {
        namfx::registerGain(*registry);
        namfx::registerTone(*registry);
        namfx::registerTs808(*registry);
        namfx::registerTransparent(*registry);
        namfx::registerMosfetOd(*registry);
        namfx::registerOtaComp(*registry);
        namfx::registerChorus(*registry);
        namfx::registerFlanger(*registry);
        namfx::registerPhaser(*registry);
        namfx::registerWah(*registry);
        namfx::registerNs2Gate(*registry);
        namfx::registerGe7Eq(*registry);
        namfx::registerDm2Delay(*registry);
        namfx::registerTapeDelay(*registry);
        namfx::registerSpringReverb(*registry);
        namfx::registerPitchShifter(*registry);
        namfx::registerOctave(*registry);
        namfx::registerHallReverb(*registry);
        namfx::registerCabIr(*registry);
        namfx::registerNamAmp(*registry);
        prepare(sample_rate, max_block_size);
    }

    void prepare(double rate, int block)
    {
        sample_rate = rate > 1000.0 ? rate : 48000.0;
        max_block_size = std::max(block, 1);
        chain_l.assign(static_cast<std::size_t>(max_block_size), 0.0f);
        chain_r.assign(static_cast<std::size_t>(max_block_size), 0.0f);
        output.prepare(sample_rate, max_block_size);
        if (chain) {
            chain->prepare(sample_rate, max_block_size);
        }
    }

    void fail(const std::string& message)
    {
        error = message;
    }
};

bool copyJsonToBuffer(const std::string& text, char* output, unsigned int capacity, unsigned int* size)
{
    if (size != nullptr) {
        *size = static_cast<unsigned int>(text.size());
    }
    if (output == nullptr || capacity == 0) {
        return true;
    }
    if (text.size() + 1 > capacity) {
        return false;
    }
    std::memcpy(output, text.data(), text.size());
    output[text.size()] = '\0';
    return true;
}

SlotDef slotFromJson(const json& value, const ModuleRegistry& registry, int fallbackSlot)
{
    const std::string module = value.at("module").get<std::string>();
    if (!registry.has(module)) {
        throw std::runtime_error("WASM 音频版不支持模块 " + module);
    }
    const std::string file = value.value("file", std::string{});

    SlotDef slot;
    slot.slot = value.value("slot", fallbackSlot);
    slot.category = value.value("category", std::string("pedal"));
    slot.impl = value.value("impl", std::string("dsp"));
    slot.moduleId = module;
    slot.file = file;
    slot.bypass = value.value("bypass", false);
    slot.mix = value.value("mix", 1.0f);

    const json params = value.value("params", json::object());
    for (const ParamSpec& spec : registry.specsFor(module)) {
        slot.params.push_back({spec.id, params.value(spec.id, spec.defaultValue)});
    }
    return slot;
}

json stateJson(const WasmEngine& engine)
{
    json state = json::object();
    state["schema"] = 1;
    state["engine"] = {
        {"sampleRate", engine.sample_rate},
        {"blockSize", engine.max_block_size},
        {"audio", true},
        {"version", "wasm-dsp"},
    };
    state["chain"] = json::array();
    if (engine.chain) {
        for (int i = 0; i < engine.chain->slotCount(); ++i) {
            const SlotDef def = engine.chain->defOf(i);
            json slot = {
                {"slot", def.slot},
                {"module", def.moduleId},
                {"assetName", ""},
                {"bypass", def.bypass},
                {"mix", engine.chain->mixValueOf(def.slot)},
                {"specs", json::array()},
                {"params", json::object()},
            };
            for (const ParamSpec& spec : engine.chain->specsOf(def.slot)) {
                slot["specs"].push_back({
                    {"id", spec.id},
                    {"name", spec.displayName},
                    {"min", spec.min},
                    {"max", spec.max},
                    {"def", spec.defaultValue},
                    {"unit", spec.unit},
                    {"taper", spec.taper == namfx::Taper::Log ? "log" : "linear"},
                });
                const std::size_t index = engine.chain->paramIndexOf(def.slot, spec.id);
                slot["params"][spec.id] = engine.chain->paramValue(def.slot, index);
            }
            state["chain"].push_back(std::move(slot));
        }
    }
    state["levels"] = {{"in", engine.input_level}, {"out", engine.output_level}};
    return state;
}

} // namespace

extern "C" {

NAMFX_WASM_EXPORT namfx_wasm* namfx_wasm_create(void)
{
    return reinterpret_cast<namfx_wasm*>(new WasmEngine());
}

NAMFX_WASM_EXPORT void namfx_wasm_destroy(namfx_wasm* opaque)
{
    delete reinterpret_cast<WasmEngine*>(opaque);
}

NAMFX_WASM_EXPORT int namfx_wasm_prepare(namfx_wasm* opaque, double sample_rate, int max_block_size)
{
    if (opaque == nullptr) {
        return -1;
    }
    auto& engine = *reinterpret_cast<WasmEngine*>(opaque);
    engine.prepare(sample_rate, max_block_size);
    return 0;
}

NAMFX_WASM_EXPORT int namfx_wasm_register_asset(namfx_wasm* opaque, const char* name, const void* data, unsigned int size)
{
    if (opaque == nullptr || name == nullptr || (data == nullptr && size > 0)) {
        return -1;
    }
    auto& engine = *reinterpret_cast<WasmEngine*>(opaque);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
    engine.assets[std::string(name)] = std::vector<std::uint8_t>(bytes, bytes + size);
    return 0;
}

NAMFX_WASM_EXPORT int namfx_wasm_load_preset_json(namfx_wasm* opaque, const char* text, unsigned int size)
{
    if (opaque == nullptr || text == nullptr) {
        return -1;
    }
    auto& engine = *reinterpret_cast<WasmEngine*>(opaque);
    try {
        const json document = json::parse(text, text + size);
        const json chain = document.at("chain");
        if (!chain.is_array()) {
            throw std::runtime_error("预设 chain 不是数组");
        }
        std::vector<SlotDef> slots;
        slots.reserve(chain.size());
        for (std::size_t i = 0; i < chain.size(); ++i) {
            slots.push_back(slotFromJson(chain.at(i), *engine.registry, static_cast<int>(i)));
        }
        const auto assetLoader = [&engine](namfx::ModuleBase& module, const SlotDef& slot) -> bool {
            const auto it = engine.assets.find(slot.file);
            if (it == engine.assets.end()) {
                throw std::runtime_error("WASM 资产未注册: " + slot.file);
            }
            return module.loadAssetBytes(it->second.data(), it->second.size());
        };
        auto next = std::make_unique<Chain>(std::move(slots), engine.registry, 12, assetLoader);
        next->prepare(engine.sample_rate, engine.max_block_size);
        engine.chain = std::move(next);
        engine.error.clear();
        return 0;
    } catch (const std::exception& exception) {
        engine.fail(exception.what());
        return -1;
    }
}

NAMFX_WASM_EXPORT int namfx_wasm_set_param(namfx_wasm* opaque, int slot, const char* param, float value)
{
    if (opaque == nullptr || param == nullptr) {
        return -1;
    }
    auto& engine = *reinterpret_cast<WasmEngine*>(opaque);
    try {
        const std::size_t index = engine.chain->paramIndexOf(slot, param);
        engine.chain->setParamByIndex(slot, index, value);
        return 0;
    } catch (const std::exception& exception) {
        engine.fail(exception.what());
        return -1;
    }
}

NAMFX_WASM_EXPORT int namfx_wasm_set_bypass(namfx_wasm* opaque, int slot, int bypass)
{
    if (opaque == nullptr || !reinterpret_cast<WasmEngine*>(opaque)->chain) {
        return -1;
    }
    reinterpret_cast<WasmEngine*>(opaque)->chain->setBypassByIndex(slot, bypass != 0);
    return 0;
}

NAMFX_WASM_EXPORT int namfx_wasm_set_mix(namfx_wasm* opaque, int slot, float mix)
{
    if (opaque == nullptr || !reinterpret_cast<WasmEngine*>(opaque)->chain) {
        return -1;
    }
    reinterpret_cast<WasmEngine*>(opaque)->chain->setMixByIndex(slot, mix);
    return 0;
}

NAMFX_WASM_EXPORT int namfx_wasm_set_output(namfx_wasm* opaque, int key, float value)
{
    if (opaque == nullptr) {
        return -1;
    }
    auto& output = reinterpret_cast<WasmEngine*>(opaque)->output;
    switch (key) {
    case 0: output.setInputGain(value); break;
    case 1: output.setMasterVolume(value); break;
    case 2: output.setBass(value); break;
    case 3: output.setMiddle(value); break;
    case 4: output.setTreble(value); break;
    case 5: output.setMute(value > 0.5f); break;
    case 6: reinterpret_cast<WasmEngine*>(opaque)->master_bypass = value > 0.5f; break;
    case 7: output.setLowCut(value); break;
    case 8: output.setHighCut(value); break;
    default: return -1;
    }
    return 0;
}

NAMFX_WASM_EXPORT void namfx_wasm_process(const namfx_wasm* opaque,
                                          const float* in_l,
                                          const float* in_r,
                                          float* out_l,
                                          float* out_r,
                                          int frames)
{
    if (opaque == nullptr || in_l == nullptr || in_r == nullptr || out_l == nullptr || out_r == nullptr || frames <= 0) {
        return;
    }
    auto& engine = *const_cast<WasmEngine*>(reinterpret_cast<const WasmEngine*>(opaque));
    if (frames > engine.max_block_size) {
        std::fill(out_l, out_l + frames, 0.0f);
        std::fill(out_r, out_r + frames, 0.0f);
        return;
    }
    float inputPeak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        inputPeak = std::max(inputPeak, std::max(std::fabs(in_l[i]), std::fabs(in_r[i])));
    }
    if (engine.master_bypass) {
        std::memcpy(out_l, in_l, static_cast<std::size_t>(frames) * sizeof(float));
        std::memcpy(out_r, in_r, static_cast<std::size_t>(frames) * sizeof(float));
    } else if (engine.chain) {
        engine.chain->process(in_l, in_r, engine.chain_l.data(), engine.chain_r.data(), frames);
        engine.output.process(engine.chain_l.data(), engine.chain_r.data(), out_l, out_r, frames);
    } else {
        std::memcpy(out_l, in_l, static_cast<std::size_t>(frames) * sizeof(float));
        std::memcpy(out_r, in_r, static_cast<std::size_t>(frames) * sizeof(float));
    }
    float outputPeak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        outputPeak = std::max(outputPeak, std::max(std::fabs(out_l[i]), std::fabs(out_r[i])));
    }
    engine.input_level = inputPeak;
    engine.output_level = outputPeak;
}

NAMFX_WASM_EXPORT void namfx_wasm_get_levels(const namfx_wasm* opaque, float* input_level, float* output_level)
{
    if (opaque == nullptr) {
        return;
    }
    const auto& engine = *reinterpret_cast<const WasmEngine*>(opaque);
    if (input_level != nullptr) *input_level = engine.input_level;
    if (output_level != nullptr) *output_level = engine.output_level;
}

NAMFX_WASM_EXPORT int namfx_wasm_state_json(const namfx_wasm* opaque,
                                            char* output,
                                            unsigned int capacity,
                                            unsigned int* size)
{
    if (opaque == nullptr) {
        return -1;
    }
    try {
        return copyJsonToBuffer(stateJson(*reinterpret_cast<const WasmEngine*>(opaque)).dump(), output, capacity, size) ? 0 : -2;
    } catch (...) {
        return -1;
    }
}

NAMFX_WASM_EXPORT const char* namfx_wasm_last_error(const namfx_wasm* opaque)
{
    if (opaque == nullptr) {
        return "WASM engine is null";
    }
    return reinterpret_cast<const WasmEngine*>(opaque)->error.c_str();
}

} // extern "C"
