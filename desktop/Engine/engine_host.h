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

    // MIDI input (control thread)
    void handleMidi(const midi::Event& event);

    // UI readouts
    const dsp::Tuner& tuner() const { return tuner_; }
    audio::OutputStage& output() { return output_; }
    audio::ControlRouter& router() { return router_; }
    audio::SceneEngine& scenes() { return scenes_; }
    const audio::Chain* chain() const { return chain_; }
    bool hasChain() const { return chain_ != nullptr; }

private:
    std::shared_ptr<ModuleRegistry> registry_;
    audio::AudioGraph graph_;
    audio::SceneEngine scenes_;
    audio::ControlRouter router_;
    audio::OutputStage output_;
    midi::MidiRouter midi_;
    midi::MidiRouter::Actions midiActions_;
    dsp::Tuner tuner_;
    const audio::Chain* chain_ = nullptr; // points at the pending/new chain
    bool prepared_ = false;
    double sampleRate_ = 48000.0;
    int blockSize_ = 64;
};

} // namespace desktop
} // namespace namfx
