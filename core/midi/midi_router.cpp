#include "midi/midi_router.h"

#include <algorithm>
#include <cmath>

namespace namfx {
namespace midi {

namespace {

// 14-bit value from an MSB/LSB CC pair (LSB in the +32 CC), defaulting to
// 7-bit when no LSB has been seen
int cc14(const Event& event, std::uint8_t lastMsb)
{
    if (event.type == Event::Type::ControlChange && event.data3 != 0) {
        return (static_cast<int>(event.data2) << 7) | static_cast<int>(event.data3);
    }
    return static_cast<int>(event.data2 != 0 ? event.data2 : lastMsb) << 7;
}

} // namespace

bool MidiRouter::learnBind(audio::ControlRouter& router, const audio::Chain& chain, int cc,
                           const std::string& moduleId, const std::string& paramId)
{
    if (cc < 0 || cc >= kCcs || sceneBinds_[static_cast<std::size_t>(cc)].active) {
        return false;
    }
    const int slot = chain.slotIndexOf(moduleId);
    if (slot < 0) {
        return false;
    }
    // resolve the parameter's spec range for CC value scaling
    // (module registry comes from the chain's modules indirectly; we query
    // through the chain's param lookup to stay registry-agnostic here)
    ParamBind& b = paramBinds_[static_cast<std::size_t>(cc)];
    if (!b.active) {
        int freeSource = -1;
        for (int i = 0; i < audio::ControlRouter::kMaxSources; ++i) {
            if (!sourceUsed_[static_cast<std::size_t>(i)]) {
                freeSource = i;
                break;
            }
        }
        if (freeSource < 0) {
            return false;
        }
        if (!router.bind(chain, freeSource, moduleId, paramId)) {
            return false;
        }
        sourceUsed_[static_cast<std::size_t>(freeSource)] = true;
        b.sourceId = freeSource;
        b.active = true;
        ++ccParamCount_;
    } else {
        // rebind: refresh the router binding for the (possibly new) target
        if (!router.bind(chain, b.sourceId, moduleId, paramId)) {
            return false;
        }
    }
    chain_ = &chain;
    router_ = &router;
    b.moduleId = moduleId;
    b.paramId = paramId;
    // spec range from the chain's registered specs
    const ParamSpec* spec = nullptr;
    for (const ParamSpec& s : chain.specsOf(slot)) {
        if (s.id == paramId) {
            spec = &s;
            break;
        }
    }
    if (spec == nullptr) {
        return false;
    }
    b.min = spec->min;
    b.max = spec->max;
    return true;
}

void MidiRouter::clearBind(int cc)
{
    if (cc < 0 || cc >= kCcs || !paramBinds_[static_cast<std::size_t>(cc)].active) {
        return;
    }
    ParamBind& b = paramBinds_[static_cast<std::size_t>(cc)];
    if (router_ != nullptr && chain_ != nullptr) {
        router_->unbind(*chain_, b.moduleId, b.paramId);
    }
    sourceUsed_[static_cast<std::size_t>(b.sourceId)] = false;
    b = ParamBind{};
    --ccParamCount_;
}

bool MidiRouter::bindScene(int cc, int sceneIndex)
{
    if (cc < 0 || cc >= kCcs || sceneIndex < 1 || sceneIndex > audio::SceneEngine::kMaxScenes
        || paramBinds_[static_cast<std::size_t>(cc)].active) {
        return false;
    }
    SceneBind& b = sceneBinds_[static_cast<std::size_t>(cc)];
    if (!b.active) {
        ++ccSceneCount_;
    }
    b.active = true;
    b.sceneIndex = sceneIndex;
    return true;
}

void MidiRouter::clearScene(int cc)
{
    if (cc < 0 || cc >= kCcs || !sceneBinds_[static_cast<std::size_t>(cc)].active) {
        return;
    }
    sceneBinds_[static_cast<std::size_t>(cc)] = SceneBind{};
    --ccSceneCount_;
}

void MidiRouter::handleEvent(const Event& event, audio::ControlRouter& router,
                             audio::SceneEngine& scenes, const Actions& actions)
{
    switch (event.type) {
    case Event::Type::ProgramChange:
        if (actions.presetRequest) {
            actions.presetRequest(static_cast<int>(event.data1));
        }
        break;
    case Event::Type::ControlChange: {
        const int cc = static_cast<int>(event.data1);
        if (cc < 0 || cc >= kCcs) {
            break;
        }
        lastMsb_[static_cast<std::size_t>(cc)] = event.data2;
        const SceneBind& scene = sceneBinds_[static_cast<std::size_t>(cc)];
        if (scene.active) {
            scenes.recall(scene.sceneIndex - 1);
            break;
        }
        const ParamBind& bind = paramBinds_[static_cast<std::size_t>(cc)];
        if (bind.active) {
            const int v14 = cc14(event, lastMsb_[static_cast<std::size_t>(cc)]);
            const float normalized = static_cast<float>(v14) / 16383.0f;
            const float value = bind.min + normalized * (bind.max - bind.min);
            router.setSourceValue(bind.sourceId, value);
        }
        break;
    }
    case Event::Type::NoteOn:
        if (event.data1 == 0x7F) { // CC 127-style global toggle via note 127
            if (actions.tunerToggle) {
                actions.tunerToggle();
            }
        }
        break;
    default:
        break;
    }
}

} // namespace midi
} // namespace namfx
