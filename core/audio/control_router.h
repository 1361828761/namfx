#pragma once

#include "audio/chain.h"
#include "audio/spsc_queue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace namfx {
namespace audio {

// Multi-source parameter arbitration (PLAN sec 5, v0.5): write priority is
// scene recall > control source > UI. A parameter bound to a control source
// (expression pedal / MIDI CC / Web UI) follows the source while it is
// active; once the source goes silent for the hang time (100-300 ms band,
// fixed 300 ms here), the target eases back to the queued UI value over the
// ParamStore ramp.
//
// Threading: bind/unbind/uiSet/setSourceValue are control-thread calls. The
// binding table is swapped via an atomic shared_ptr (no locks, no audio-side
// allocation). Source liveness uses a heartbeat counter, not clocks: every
// setSourceValue bumps the source heartbeat; the audio thread marks the
// source active when it observes a new heartbeat and falls back after the
// hang time. UI pending values live in a fixed-size audio-thread-owned array
// (reset when the table pointer changes), so the audio thread never mutates
// shared state.
class ControlRouter {
public:
    static constexpr int kMaxSources = 16;
    static constexpr int kMaxBindings = 32;
    static constexpr long kHangTimeMs = 300;

    // ---- control thread -------------------------------------------------
    // resolve moduleId/paramId against the chain and (re)bind; binding an
    // already-bound parameter to a DIFFERENT source is rejected
    bool bind(const Chain& chain, int sourceId, const std::string& moduleId,
              const std::string& paramId);
    void unbind(const Chain& chain, const std::string& moduleId, const std::string& paramId);
    bool isBound(const std::string& moduleId, const std::string& paramId) const;
    int boundCount() const;

    // latest value + liveness heartbeat of a control source (latest-wins)
    void setSourceValue(int sourceId, float value);

    // UI parameter write: enqueued to the audio thread; bound params keep
    // their value queued (deep-1) until the source is released
    bool uiSet(const std::string& moduleId, const std::string& paramId, float value);

    // UI bypass write: same queue, applied at the block boundary on the
    // audio thread (bypass flips module fade state, so it must not be
    // touched from the control thread)
    bool uiSetBypass(const std::string& moduleId, bool bypass);

    // ---- audio thread (block boundary, after SceneEngine::apply) --------
    void apply(Chain& chain, int frames);

private:
    struct Binding {
        int sourceId = -1;
        int slot = -1;
        std::size_t paramIndex = 0;
        std::string key; // moduleId + '/' + paramId
    };
    struct Table {
        std::vector<Binding> bindings;
        std::unordered_map<std::string, std::size_t> index; // key -> binding index
    };
    struct UiCommand {
        std::string key;     // pre-built lookup key (control thread)
        std::string moduleId;
        std::string paramId;
        float value = 0.0f;
        bool isBypass = false;
    };
    struct Pending {
        bool active = false;
        float value = 0.0f;
        long lastHeartbeat = -1;
        long lastActiveTick = -1;
    };

    std::atomic<const Table*> table_{nullptr};
    std::array<std::atomic<float>, kMaxSources> sourceValues_{};
    std::array<std::atomic<long>, kMaxSources> sourceHeartbeat_{};
    SpscQueue<UiCommand, 64> uiQueue_;
    // pop target reused across blocks: constructing std::string on the
    // audio thread allocates (MSVC debug iterator proxies), so the buffer
    // is a member built once on the control thread
    UiCommand uiCmdBuf_;
    // audio thread pushes tables it stopped using here; control thread
    // recycles them (RT-safe deferred deletion)
    SpscQueue<const Table*, 8> releaseQueue_;
    // audio-thread owned, fixed size; reset whenever the table pointer changes
    std::array<Pending, kMaxBindings> pending_{};
    const Table* lastTable_ = nullptr;
    long tick_ = 0;
    double sampleRate_ = 48000.0;
};

} // namespace audio
} // namespace namfx
