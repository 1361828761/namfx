#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace namfx {

namespace preset {
struct SceneDef;
}

namespace audio {

class Chain;

// Engine-side scene bank (PLAN M5b): up to 8 scenes per preset, each a set
// of module bypasses + parameter combinations. Scene actions are pre-resolved
// to slot/param indices on load (control thread), so the audio-thread
// application path is index-based and allocation-free.
//
// Threading: recall() is control-thread (may also come from a footswitch
// command via the engine queue); apply() runs on the audio thread at the
// block boundary and atomically applies the newest pending scene. Parameter
// changes ride the ParamStore 10 ms ramp (smooth); bypass changes ride the
// per-slot fade (immediate semantics per PLAN: scene recall is not a chain
// swap, no dry-through).
//
// Graph exchange note: after a preset swap the bank must be reloaded
// (SceneEngine::load) -- apply() drops actions whose slot no longer exists.
class SceneEngine {
public:
    static constexpr int kMaxScenes = 8;

    // control thread: validate and pre-resolve all scenes against the chain;
    // returns false when scene count exceeds the cap or an override
    // references an unknown module or parameter
    bool load(const std::vector<preset::SceneDef>& scenes, const Chain& chain);
    int sceneCount() const { return static_cast<int>(scenes_.size()); }

    const std::string& sceneName(int index) const;

    // control thread: request a scene switch; applied at the next block
    void recall(int index);
    int pendingScene() const { return pending_.load(); }
    int activeScene() const { return active_.load(); }

    // audio thread, block boundary, before Chain::process: applies the
    // pending recall (if any); zero allocation
    void apply(Chain& chain);

private:
    struct SceneAction {
        int slot = -1;
        std::size_t paramIndex = 0;
        float value = 0.0f;
        bool hasParam = false;
        bool hasBypass = false;
        bool bypass = false;
    };
    struct Scene {
        std::string name;
        std::vector<SceneAction> actions;
    };

    std::vector<Scene> scenes_;
    std::atomic<int> pending_{-1};
    std::atomic<int> active_{-1};
};

} // namespace audio
} // namespace namfx
