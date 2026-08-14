#pragma once

#include "audio/audio_graph.h"
#include "audio/control_router.h"
#include "audio/output_stage.h"
#include "audio/scene_engine.h"
#include "dsp/tuner.h"
#include "midi/midi_router.h"
#include "modules/module_registry.h"
#include "preset/preset_model.h"

#include <memory>
#include <mutex>
#include <string>

namespace namfx {
namespace desktop {

// Engine host for the desktop editor: owns the audio graph (double-buffered
// swap), the scene bank, the control-source router, the output stage and
// the tuner, and routes UI/MIDI commands. The same engine pieces the
// embedded device runs; the host only adds the control-thread API surface.
//
// Threading: prepare()/process() run on the audio thread; everything else
// is control-thread (the JUCE message thread). loadPreset() performs the
// graph swap. NOTE: after a successful load the UI must re-fetch the chain
// reference (chain() is invalidated by the next swap).
class EngineHost {
public:
    EngineHost();

    // audio thread
    void prepare(double sampleRate, int blockSize);
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

    // control thread: master bypass (true = clean input passthrough, the
    // whole engine chain is skipped); atomic, effective from the next block
    void setBypass(bool bypass) { bypass_.store(bypass, std::memory_order_relaxed); }
    bool bypass() const { return bypass_.load(std::memory_order_relaxed); }

    // control thread
    bool loadPreset(const std::string& jsonPath, const std::string& baseDir, std::string& error);
    bool loadPresetText(const std::string& jsonText, const std::string& baseDir,
                        std::string& error);

    void recallScene(int index);
    int activeScene() const { return scenes_.activeScene(); }
    int sceneCount() const { return scenes_.sceneCount(); }
    const std::string& sceneName(int index) const { return scenes_.sceneName(index); }

    // UI writes (through the router: bound params queue, unbound apply at
    // the next block); slot is the chain slot index
    bool uiSetParam(int slot, const std::string& paramId, float value);
    bool uiSetBypass(int slot, bool bypass);

    // UI readouts
    // control-thread chain summary ("slot: module  param=value ...") for
    // the UI chain view; reads inside the chain mutex
    std::string chainSummary() const;

    // chain editing (M5c): add a module at the end / remove one by slot;
    // the edit snapshots the live chain (current parameter values), rebuilds
    // a fresh chain and swaps it in (fade-in, no pop). assetFile is the
    // model/IR path for nam.amp / cab.ir (may be empty for dsp modules)
    bool addModuleToChain(const std::string& moduleId, const std::string& assetFile,
                          std::string& error);
    bool removeModuleFromChain(int slot, std::string& error);
    bool moveModuleTo(int slot, int dstIndex, std::string& error); // drag reorder
    bool moveModule(int slot, int direction, std::string& error);  // up/down buttons
    std::vector<std::string> moduleIds() const;

    // save the live chain as a user preset (schema 1 JSON) into dir
    bool savePreset(const std::string& name, const std::string& dir, std::string& error);

    // structured chain readout for the edit panel
    struct SlotInfo {
        int slot = 0;
        std::string moduleId;
        bool bypass = false;
        float mix = 1.0f;
        std::vector<ParamSpec> specs;
        std::vector<float> values;
    };
    std::vector<SlotInfo> chainInfo() const;

    // MIDI input (control thread)
    void handleMidi(const midi::Event& event);

    // UI readouts
    const dsp::Tuner& tuner() const { return tuner_; }
    audio::OutputStage& output() { return output_; }
    audio::ControlRouter& router() { return router_; }
    audio::SceneEngine& scenes() { return scenes_; }

private:
    // rebuild the graph from a slot list (control thread, chain mutex held)
    bool rebuildChain(std::vector<audio::SlotDef> slots, std::string& error);

    // guards chain_ against device reconfiguration (prepare() re-prepares
    // the live chain) racing preset loads; never taken inside the audio
    // callback (process() does not touch chain_)
    mutable std::mutex chainMutex_;

    std::shared_ptr<ModuleRegistry> registry_;
    audio::AudioGraph graph_;
    audio::SceneEngine scenes_;
    audio::ControlRouter router_;
    audio::OutputStage output_;
    midi::MidiRouter midi_;
    midi::MidiRouter::Actions midiActions_;
    dsp::Tuner tuner_;
    audio::Chain* chain_ = nullptr; // points at the pending/new chain
    std::atomic<bool> bypass_{false};
    bool prepared_ = false;
    double sampleRate_ = 48000.0;
    int blockSize_ = 64;
};

} // namespace desktop
} // namespace namfx
