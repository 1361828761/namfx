#pragma once

#include "audio/control_router.h"
#include "audio/scene_engine.h"
#include "midi/midi_events.h"
#include "modules/module_registry.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace namfx {
namespace midi {

// Engine-side MIDI routing (PLAN G4 / M5c): maps incoming MIDI messages to
// engine actions on the control thread:
//  - CC bound to a parameter acts as a control source (14-bit MSB/LSB pair,
//    scaled into the parameter's spec range); the binding feeds
//    ControlRouter::setSourceValue
//  - CC bound to a scene recalls it via SceneEngine
//  - Program Change fires the preset-request callback (the host performs the
//    graph swap)
//  - a global-bypass / tuner toggle callback is available
// Learning is explicit: learnBind() (re)binds a CC number, clearBind()
// removes it. A CC number bound to a parameter and to a scene at once is
// rejected.
class MidiRouter {
public:
    // control-thread callbacks consumed by the host (desktop shell, later
    // the Web UI / embedded LCD)
    struct Actions {
        std::function<void(int presetIndex)> presetRequest;
        std::function<void()> globalBypassToggle;
        std::function<void()> tunerToggle;
    };

    // (re)bind a CC number to a parameter (a control source); false when the
    // CC is already bound to a scene, the parameter is unknown, or the
    // source table is full. Also registers the parameter with the
    // ControlRouter so the source value reaches the chain.
    // The chain must outlive the router (host responsibility).
    bool learnBind(audio::ControlRouter& router, const audio::Chain& chain, int cc,
                   const std::string& moduleId, const std::string& paramId);
    void clearBind(int cc);

    // bind a CC number to a scene (1..SceneEngine::kMaxScenes); false when
    // the CC is already bound to a parameter
    bool bindScene(int cc, int sceneIndex);
    void clearScene(int cc);

    // process one incoming event (control thread)
    void handleEvent(const Event& event, audio::ControlRouter& router,
                     audio::SceneEngine& scenes, const Actions& actions);

    int ccParamCount() const { return ccParamCount_; }
    int ccSceneCount() const { return ccSceneCount_; }

private:
    struct ParamBind {
        bool active = false;
        int sourceId = -1;
        std::string moduleId;
        std::string paramId;
        float min = 0.0f;
        float max = 1.0f;
    };
    struct SceneBind {
        bool active = false;
        int sceneIndex = -1; // 1-based scene number
    };

    static constexpr int kCcs = 128;
    std::array<ParamBind, kCcs> paramBinds_{};
    std::array<SceneBind, kCcs> sceneBinds_{};
    std::array<bool, audio::ControlRouter::kMaxSources> sourceUsed_{};
    const audio::Chain* chain_ = nullptr;
    audio::ControlRouter* router_ = nullptr;
    int ccParamCount_ = 0;
    int ccSceneCount_ = 0;
    std::uint8_t lastMsb_[kCcs]{};
};

} // namespace midi
} // namespace namfx
