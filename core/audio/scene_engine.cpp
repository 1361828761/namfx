#include "audio/scene_engine.h"

#include "audio/chain.h"
#include "preset/preset_model.h"

#include <stdexcept>
#include <utility>

namespace namfx {
namespace audio {

bool SceneEngine::load(const std::vector<preset::SceneDef>& scenes, const Chain& chain)
{
    if (scenes.size() > static_cast<std::size_t>(kMaxScenes)) {
        return false;
    }
    std::vector<Scene> built;
    built.reserve(scenes.size());
    for (const preset::SceneDef& def : scenes) {
        Scene scene;
        scene.name = def.name;
        scene.actions.reserve(def.overrides.size());
        for (const preset::SceneOverride& override : def.overrides) {
            const int slot = chain.slotIndexOf(override.moduleId);
            if (slot < 0) {
                return false;
            }
            for (const ParamInit& init : override.params) {
                SceneAction action;
                action.slot = slot;
                action.hasParam = true;
                action.value = init.value;
                try {
                    action.paramIndex = chain.paramIndexOf(slot, init.id);
                } catch (const std::out_of_range&) {
                    return false;
                }
                scene.actions.push_back(action);
            }
            SceneAction bypass;
            bypass.slot = slot;
            bypass.hasBypass = true;
            bypass.bypass = override.bypass;
            scene.actions.push_back(bypass);
        }
        built.push_back(std::move(scene));
    }
    scenes_ = std::move(built);
    pending_ = -1;
    active_ = -1;
    return true;
}

const std::string& SceneEngine::sceneName(int index) const
{
    return scenes_.at(static_cast<std::size_t>(index)).name;
}

void SceneEngine::recall(int index)
{
    if (index < 0 || index >= sceneCount()) {
        return;
    }
    pending_ = index;
}

void SceneEngine::apply(Chain& chain)
{
    const int index = pending_.exchange(-1);
    if (index < 0) {
        return;
    }
    const Scene& scene = scenes_.at(static_cast<std::size_t>(index));
    for (const SceneAction& action : scene.actions) {
        if (action.slot >= chain.slotCount()) {
            continue; // graph swapped underneath: drop safely
        }
        if (action.hasParam) {
            chain.setParamByIndex(action.slot, action.paramIndex, action.value);
        }
        if (action.hasBypass) {
            chain.setBypassByIndex(action.slot, action.bypass);
        }
    }
    active_ = index;
}

} // namespace audio
} // namespace namfx
