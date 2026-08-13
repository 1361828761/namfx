#include "preset/preset_io.h"

#include "modules/module_registry.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace namfx {
namespace preset {

namespace {

constexpr int kMaxSceneNameLength = 8;

float clampToSpec(const ParamSpec& spec, float value)
{
    return std::min(std::max(value, spec.min), spec.max);
}

bool loadParams(const nlohmann::json& paramsNode, const ModuleRegistry& registry,
                const std::string& moduleId, LoadMode mode, LoadReport& report,
                std::vector<ParamInit>& out)
{
    if (!paramsNode.is_object()) {
        if (mode == LoadMode::Strict) {
            report.errors.push_back("params for module '" + moduleId + "' must be an object");
            return false;
        }
        report.warnings.push_back("params for module '" + moduleId + "' ignored: not an object");
        return true;
    }
    for (auto it = paramsNode.begin(); it != paramsNode.end(); ++it) {
        const std::string paramId = it.key();
        const ParamSpec* spec = registry.findParam(moduleId, paramId);
        if (spec == nullptr) {
            if (mode == LoadMode::Strict) {
                report.errors.push_back("unknown param '" + paramId + "' for module '"
                                        + moduleId + "'");
                return false;
            }
            report.warnings.push_back("unknown param '" + paramId + "' for module '"
                                      + moduleId + "' ignored");
            continue;
        }
        if (!it.value().is_number()) {
            if (mode == LoadMode::Strict) {
                report.errors.push_back("param '" + paramId + "' must be a number");
                return false;
            }
            report.warnings.push_back("param '" + paramId + "' ignored: not a number");
            continue;
        }
        const float raw = it.value().get<float>();
        const float clamped = clampToSpec(*spec, raw);
        if (std::fabs(clamped - raw) > 1e-6f) {
            report.warnings.push_back("param '" + paramId + "' of module '" + moduleId
                                      + "' clamped to [" + std::to_string(spec->min) + ", "
                                      + std::to_string(spec->max) + "]");
        }
        out.push_back(ParamInit{paramId, clamped});
    }
    return true;
}

bool loadSlot(const nlohmann::json& slotNode, const ModuleRegistry& registry, LoadMode mode,
              LoadReport& report, audio::SlotDef& out)
{
    const bool hasRequired = slotNode.contains("slot") && slotNode.contains("category")
        && slotNode.contains("impl") && slotNode.contains("module");
    const bool typesOk = hasRequired && slotNode["slot"].is_number_integer()
        && slotNode["category"].is_string() && slotNode["impl"].is_string()
        && slotNode["module"].is_string();
    if (!typesOk) {
        if (mode == LoadMode::Strict) {
            report.errors.push_back("slot missing required field (slot/category/impl/module)");
            return false;
        }
        report.warnings.push_back("slot skipped: missing required field");
        return true;
    }
    out.slot = slotNode["slot"].get<int>();
    out.category = slotNode["category"].get<std::string>();
    out.impl = slotNode["impl"].get<std::string>();
    out.moduleId = slotNode["module"].get<std::string>();
    if (!registry.has(out.moduleId)) {
        if (mode == LoadMode::Strict) {
            report.errors.push_back("unknown module id: " + out.moduleId);
            return false;
        }
        report.warnings.push_back("unknown module id '" + out.moduleId + "': slot bypassed");
        out.bypass = true;
        return true;
    }
    if (slotNode.contains("params")
        && !loadParams(slotNode["params"], registry, out.moduleId, mode, report, out.params)) {
        return false;
    }
    if (slotNode.contains("bypass")) {
        if (!slotNode["bypass"].is_boolean()) {
            if (mode == LoadMode::Strict) {
                report.errors.push_back("slot bypass must be boolean");
                return false;
            }
            report.warnings.push_back("slot bypass ignored: not boolean");
        } else {
            out.bypass = slotNode["bypass"].get<bool>();
        }
    }
    if (slotNode.contains("mix")) {
        if (!slotNode["mix"].is_number()) {
            if (mode == LoadMode::Strict) {
                report.errors.push_back("slot mix must be a number");
                return false;
            }
            report.warnings.push_back("slot mix ignored: not a number");
        } else {
            const float rawMix = slotNode["mix"].get<float>();
            const float clampedMix = std::min(std::max(rawMix, 0.0f), 1.0f);
            if (std::fabs(clampedMix - rawMix) > 1e-6f) {
                report.warnings.push_back("slot mix clamped to [0, 1]");
            }
            out.mix = clampedMix;
        }
    }
    return true;
}

bool loadScenes(const nlohmann::json& scenesNode, const ModuleRegistry& registry, LoadMode mode,
                LoadReport& report, std::vector<SceneDef>& out)
{
    if (!scenesNode.is_array()) {
        if (mode == LoadMode::Strict) {
            report.errors.push_back("scenes must be an array");
            return false;
        }
        report.warnings.push_back("scenes ignored: not an array");
        return true;
    }
    std::size_t count = scenesNode.size();
    if (count > static_cast<std::size_t>(kMaxScenes)) {
        report.warnings.push_back("scenes truncated to " + std::to_string(kMaxScenes));
        count = static_cast<std::size_t>(kMaxScenes);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const nlohmann::json& sceneNode = scenesNode[i];
        if (!sceneNode.is_object()) {
            continue;
        }
        SceneDef scene;
        if (sceneNode.contains("name") && sceneNode["name"].is_string()) {
            scene.name = sceneNode["name"].get<std::string>();
            if (scene.name.size() > kMaxSceneNameLength) {
                report.warnings.push_back("scene name truncated to "
                                          + std::to_string(kMaxSceneNameLength) + " chars");
                scene.name.resize(kMaxSceneNameLength);
            }
        }
        if (sceneNode.contains("overrides") && sceneNode["overrides"].is_array()) {
            for (const nlohmann::json& overrideNode : sceneNode["overrides"]) {
                if (!overrideNode.is_object() || !overrideNode.contains("moduleId")
                    || !overrideNode["moduleId"].is_string()) {
                    continue;
                }
                SceneOverride overrideDef;
                overrideDef.moduleId = overrideNode["moduleId"].get<std::string>();
                if (!registry.has(overrideDef.moduleId)) {
                    if (mode == LoadMode::Strict) {
                        report.errors.push_back("scene override for unknown module '"
                                                + overrideDef.moduleId + "'");
                        return false;
                    }
                    report.warnings.push_back("scene override for unknown module '"
                                              + overrideDef.moduleId + "' ignored");
                    continue;
                }
                if (overrideNode.contains("bypass") && overrideNode["bypass"].is_boolean()) {
                    overrideDef.bypass = overrideNode["bypass"].get<bool>();
                }
                if (overrideNode.contains("params")
                    && !loadParams(overrideNode["params"], registry, overrideDef.moduleId, mode,
                                   report, overrideDef.params)) {
                    return false;
                }
                scene.overrides.push_back(std::move(overrideDef));
            }
        }
        out.push_back(std::move(scene));
    }
    return true;
}

} // namespace

Preset loadPreset(const std::string& jsonText, LoadMode mode, const ModuleRegistry& registry,
                  LoadReport& report)
{
    Preset preset;
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(jsonText);
    } catch (const nlohmann::json::exception&) {
        report.errors.push_back("malformed JSON");
        return preset;
    }
    if (!doc.is_object()) {
        report.errors.push_back("preset root must be an object");
        return preset;
    }

    int schema = 0;
    if (doc.contains("schema")) {
        if (!doc["schema"].is_number_integer()) {
            report.errors.push_back("schema must be an integer");
            return preset;
        }
        schema = doc["schema"].get<int>();
    }
    if (schema > kCurrentSchema) {
        report.errors.push_back("preset schema " + std::to_string(schema)
                                + " is newer than supported schema " + std::to_string(kCurrentSchema));
        return preset;
    }
    if (schema < kCurrentSchema) {
        std::string migratedText = jsonText;
        std::string error;
        migratedText = migrate(std::move(migratedText), schema, error);
        if (!error.empty()) {
            report.errors.push_back(error);
            return preset;
        }
        try {
            doc = nlohmann::json::parse(migratedText);
        } catch (const nlohmann::json::exception&) {
            report.errors.push_back("migration produced malformed JSON");
            return preset;
        }
    }
    preset.schema = schema;

    if (doc.contains("name") && doc["name"].is_string()) {
        preset.name = doc["name"].get<std::string>();
    } else if (mode == LoadMode::Strict) {
        report.errors.push_back("preset name is required");
        return preset;
    } else {
        preset.name = "Untitled";
        report.warnings.push_back("preset name missing, defaulting to Untitled");
    }

    if (!doc.contains("chain") || !doc["chain"].is_array()) {
        report.errors.push_back("preset chain must be an array");
        return preset;
    }
    for (const nlohmann::json& slotNode : doc["chain"]) {
        if (!slotNode.is_object()) {
            if (mode == LoadMode::Strict) {
                report.errors.push_back("slot must be an object");
                return preset;
            }
            report.warnings.push_back("slot skipped: not an object");
            continue;
        }
        audio::SlotDef def;
        if (!loadSlot(slotNode, registry, mode, report, def)) {
            return preset;
        }
        if (def.moduleId.empty()) {
            continue;
        }
        preset.chain.push_back(std::move(def));
    }

    if (doc.contains("scenes")) {
        if (!loadScenes(doc["scenes"], registry, mode, report, preset.scenes)) {
            return preset;
        }
    }

    return preset;
}

std::string savePreset(const Preset& preset)
{
    nlohmann::json doc;
    doc["schema"] = kCurrentSchema;
    doc["name"] = preset.name;
    nlohmann::json chain = nlohmann::json::array();
    for (const audio::SlotDef& slot : preset.chain) {
        nlohmann::json slotNode;
        slotNode["slot"] = slot.slot;
        slotNode["category"] = slot.category;
        slotNode["impl"] = slot.impl;
        slotNode["module"] = slot.moduleId;
        nlohmann::json params = nlohmann::json::object();
        for (const ParamInit& param : slot.params) {
            params[param.id] = param.value;
        }
        slotNode["params"] = std::move(params);
        slotNode["bypass"] = slot.bypass;
        slotNode["mix"] = slot.mix;
        chain.push_back(std::move(slotNode));
    }
    doc["chain"] = std::move(chain);
    if (!preset.scenes.empty()) {
        nlohmann::json scenes = nlohmann::json::array();
        for (const SceneDef& scene : preset.scenes) {
            nlohmann::json sceneNode;
            sceneNode["name"] = scene.name;
            nlohmann::json overrides = nlohmann::json::array();
            for (const SceneOverride& overrideDef : scene.overrides) {
                nlohmann::json overrideNode;
                overrideNode["moduleId"] = overrideDef.moduleId;
                nlohmann::json params = nlohmann::json::object();
                for (const ParamInit& param : overrideDef.params) {
                    params[param.id] = param.value;
                }
                overrideNode["params"] = std::move(params);
                overrideNode["bypass"] = overrideDef.bypass;
                overrides.push_back(std::move(overrideNode));
            }
            sceneNode["overrides"] = std::move(overrides);
            scenes.push_back(std::move(sceneNode));
        }
        doc["scenes"] = std::move(scenes);
    }
    return doc.dump(2);
}

} // namespace preset
} // namespace namfx
