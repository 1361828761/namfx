#include "desktop/Engine/engine_host.h"

#include "audio/chain.h"
#include "modules/dsp/chorus.h"
#include "modules/dsp/dm2_delay.h"
#include "modules/dsp/flanger.h"
#include "modules/dsp/gain.h"
#include "modules/dsp/ge7_eq.h"
#include "modules/dsp/hall_reverb.h"
#include "modules/dsp/klon.h"
#include "modules/dsp/ns2_gate.h"
#include "modules/dsp/ocd.h"
#include "modules/dsp/octave.h"
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
#include "preset/preset_io.h"

#include <fstream>
#include <sstream>

namespace namfx {
namespace desktop {

namespace {

std::string readFileText(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

EngineHost::EngineHost()
{
    registry_ = std::make_shared<ModuleRegistry>();
    registerGain(*registry_);
    registerTone(*registry_);
    registerTs808(*registry_);
    registerTransparent(*registry_);
    registerMosfetOd(*registry_);
    registerOtaComp(*registry_);
    registerChorus(*registry_);
    registerFlanger(*registry_);
    registerPhaser(*registry_);
    registerWah(*registry_);
    registerNs2Gate(*registry_);
    registerGe7Eq(*registry_);
    registerDm2Delay(*registry_);
    registerTapeDelay(*registry_);
    registerSpringReverb(*registry_);
    registerPitchShifter(*registry_);
    registerOctave(*registry_);
    registerHallReverb(*registry_);
    registerCabIr(*registry_);
    registerNamAmp(*registry_);
    graph_.setRegistry(registry_);
}

void EngineHost::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    output_.prepare(sampleRate, blockSize);
    tuner_.prepare(sampleRate);
    prepared_ = true;
}

void EngineHost::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }
    graph_.processBlock(inL, inR, outL, outR, n);
    output_.process(outL, outR, outL, outR, n);
    tuner_.process(inL, n);
}

bool EngineHost::loadPreset(const std::string& jsonPath, const std::string& baseDir,
                            std::string& error)
{
    const std::string text = readFileText(jsonPath);
    if (text.empty()) {
        error = "cannot read preset file";
        return false;
    }
    return loadPresetText(text, baseDir, error);
}

bool EngineHost::loadPresetText(const std::string& jsonText, const std::string& baseDir,
                                std::string& error)
{
    preset::LoadReport report;
    const preset::Preset preset =
        preset::loadPreset(jsonText, preset::LoadMode::Strict, *registry_, report, baseDir);
    if (!report.ok()) {
        error = "preset rejected:";
        for (const std::string& e : report.errors) {
            error += " " + e;
        }
        return false;
    }
    auto chain = std::make_unique<audio::Chain>(preset.chain, registry_);
    chain->prepare(sampleRate_, blockSize_);
    // UI parameter writes target this chain directly (graph-swap protocol:
    // edits on the incoming chain, effective once the swap lands); chain_
    // stays valid because the graph owns the chain after the swap
    chain_ = chain.get();
    graph_.requestSwap(std::move(chain));
    if (!scenes_.load(preset.scenes, *chain_)) {
        error = "scene bank rejected";
        return false;
    }
    return true;
}

void EngineHost::recallScene(int index)
{
    scenes_.recall(index);
}

bool EngineHost::uiSetParam(int slot, const std::string& paramId, float value)
{
    const audio::Chain* active = chain();
    if (active == nullptr) {
        return false;
    }
    std::string moduleId;
    try {
        moduleId = active->moduleIdOf(slot);
    } catch (const std::out_of_range&) {
        return false;
    }
    return router_.uiSet(moduleId, paramId, value);
}

bool EngineHost::uiSetBypass(int slot, bool bypass)
{
    const audio::Chain* active = chain();
    if (active == nullptr) {
        return false;
    }
    std::string moduleId;
    try {
        moduleId = active->moduleIdOf(slot);
    } catch (const std::out_of_range&) {
        return false;
    }
    // fade state is audio-thread owned: queue the write like any UI command
    return router_.uiSetBypass(moduleId, bypass);
}

void EngineHost::handleMidi(const midi::Event& event)
{
    midi_.handleEvent(event, router_, scenes_, midiActions_);
}

} // namespace desktop
} // namespace namfx
