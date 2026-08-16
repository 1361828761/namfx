#include "desktop/Engine/engine_host.h"

#include "audio/chain.h"
#include "audio/chain_builder.h"
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace namfx {
namespace desktop {

namespace {

constexpr int kMaxChainSlots = 12;

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

std::string formatParam(const ParamSpec& spec, float value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
    std::string s(buf);
    if (!spec.unit.empty()) {
        s += spec.unit;
    }
    return s;
}

// module id -> slot impl: the engine distinguishes dsp / ir / nam modules
std::string implForModule(const std::string& id)
{
    if (id == "cab.ir") {
        return "ir";
    }
    if (id == "amp.nam") {
        return "nam";
    }
    return "dsp";
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
    router_.setSampleRate(sampleRate); // hang-time scales with the device rate
    {
        // device reconfiguration (block size / sample rate change): the
        // live chain must follow, or Chain::process asserts on n > maxBlock
        // (Debug) / overruns its scratch buffers (Release). prepareToPlay
        // never runs concurrently with the callback, so the lock only
        // serializes against preset loads on the control thread.
        std::lock_guard<std::mutex> lock(chainMutex_);
        if (chain_ != nullptr) {
            chain_->prepare(sampleRate, blockSize);
        }
    }
    prepared_ = true;
}

void EngineHost::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if (!prepared_ || bypass_.load(std::memory_order_relaxed)) {
        // master bypass: clean passthrough, the engine is skipped entirely
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }
    // The chain's scratch buffers are sized to blockSize_ (prepared). Some
    // drivers deliver callbacks larger than the expected block; split into
    // blockSize_ chunks so Chain::process never exceeds maxBlock_ (a Debug
    // assert / Release overrun otherwise).
    float inPeak = 0.0f;
    float outPeak = 0.0f;
    const int step = blockSize_ > 0 ? blockSize_ : n;
    for (int off = 0; off < n; off += step) {
        const int count = std::min(step, n - off);
        graph_.processBlock(inL + off, inR + off, outL + off, outR + off, count);
        // Block boundary: apply queued scene recalls (priority 1) and UI /
        // control-source writes (priority 2) to the LIVE chain. Runs after
        // processBlock so a swap landing in this block already targeted the
        // new chain -- the application itself only takes effect from the
        // next block (params ramp, bypass fades), which is the documented
        // scene/control-source contract.
        audio::Chain* live = graph_.live();
        if (live != nullptr) {
            scenes_.apply(*live);
            router_.apply(*live, count);
        }        output_.process(outL + off, outR + off, outL + off, outR + off, count);
        tuner_.process(inL + off, count);
        for (int i = 0; i < count; ++i) {
            const float a = std::fabs(inL[off + i]);
            const float b = std::fabs(outL[off + i]);
            inPeak = std::max(inPeak, a);
            outPeak = std::max(outPeak, b);
        }
    }
    // level readouts for the UI: block peak with exponential decay
    const float prevIn = inLevel_.load(std::memory_order_relaxed);
    const float prevOut = outLevel_.load(std::memory_order_relaxed);
    inLevel_.store(std::max(inPeak, prevIn * 0.999f), std::memory_order_relaxed);
    outLevel_.store(std::max(outPeak, prevOut * 0.999f), std::memory_order_relaxed);
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
    auto chain = std::make_unique<audio::Chain>(preset.chain, registry_, kMaxChainSlots);
    chain->prepare(sampleRate_, blockSize_);
    chain->startFadeIn(); // swap eases in from dry: no pop on preset load
    {
        // UI parameter writes target this chain directly (graph-swap
        // protocol: edits on the incoming chain, effective once the swap
        // lands); chain_ stays valid because the graph owns the chain
        std::lock_guard<std::mutex> lock(chainMutex_);
        chain_ = chain.get();
        midi_.setChain(*chain_);
        graph_.requestSwap(std::move(chain));
        scenesDefs_ = preset.scenes;
        if (!scenes_.load(preset.scenes, *chain_)) {
            error = "scene bank rejected";
            return false;
        }
    }
    return true;
}

void EngineHost::recallScene(int index)
{
    scenes_.recall(index);
}

bool EngineHost::saveScene(int index, const std::string& name, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    if (index < 0 || index > static_cast<int>(scenesDefs_.size())) {
        error = "bad scene index";
        return false;
    }
    if (index == static_cast<int>(scenesDefs_.size())
        && scenesDefs_.size() >= static_cast<std::size_t>(audio::SceneEngine::kMaxScenes)) {
        error = "scene limit reached (8)";
        return false;
    }
    // snapshot the live chain into a scene definition
    preset::SceneDef def;
    def.name = name.empty() ? ("Scene " + std::to_string(index + 1)) : name;
    const std::vector<audio::SlotDef> slots = audio::snapshotChain(*chain_);
    for (const audio::SlotDef& s : slots) {
        preset::SceneOverride ov;
        ov.moduleId = s.moduleId;
        ov.bypass = s.bypass;
        ov.params = s.params; // current (ramped) values
        def.overrides.push_back(std::move(ov));
    }
    const auto backup = scenesDefs_;
    if (index == static_cast<int>(scenesDefs_.size())) {
        scenesDefs_.push_back(std::move(def));
    } else {
        scenesDefs_[static_cast<std::size_t>(index)] = std::move(def);
    }
    if (!scenes_.load(scenesDefs_, *chain_)) {
        scenesDefs_ = backup;
        scenes_.load(backup, *chain_);
        error = "scene rejected by the engine";
        return false;
    }
    return true;
}

std::vector<preset::SceneDef> EngineHost::sceneDefs() const
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    return scenesDefs_;
}

bool EngineHost::uiSetParam(int slot, const std::string& paramId, float value)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    const audio::Chain* active = chain_;
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

bool EngineHost::uiSetMix(int slot, float mix)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        return false;
    }
    try {
        chain_->setMixByIndex(slot, mix);
    } catch (const std::out_of_range&) {
        return false;
    }
    return true;
}
bool EngineHost::uiSetBypass(int slot, bool bypass)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    const audio::Chain* active = chain_;
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

std::string EngineHost::chainSummary() const
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        return "(no preset loaded)";
    }
    std::string s;
    for (int i = 0; i < chain_->slotCount(); ++i) {
        std::string moduleId;
        try {
            moduleId = chain_->moduleIdOf(i);
        } catch (const std::out_of_range&) {
            continue;
        }
        s += std::to_string(i) + ": " + moduleId;
        const std::vector<ParamSpec>& specs = chain_->specsOf(i);
        for (std::size_t p = 0; p < specs.size(); ++p) {
            const float v = chain_->paramValue(i, p);
            s += "  " + specs[p].id + "=" + formatParam(specs[p], v);
        }
        s += '\n';
    }
    return s;
}

bool EngineHost::rebuildChain(std::vector<audio::SlotDef> slots, std::string& error)
{
    try {
        auto chain = std::make_unique<audio::Chain>(std::move(slots), registry_, kMaxChainSlots);
        chain->prepare(sampleRate_, blockSize_);
        chain->startFadeIn(); // the swap eases in: no pop
        chain_ = chain.get();
        midi_.setChain(*chain_);
        graph_.requestSwap(std::move(chain));
        return true;
    } catch (const std::exception& e) {
        // asset load / parameter validation can throw (Chain ctor): never
        // let that escape the UI thread (it crashed the app before)
        error = e.what();
        return false;
    }
}

bool EngineHost::addModuleToChain(const std::string& moduleId, const std::string& assetFile,
                                  std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    if (chain_->slotCount() >= kMaxChainSlots) {
        error = "chain limit reached (12 slots)";
        return false;
    }
    if (!registry_->has(moduleId)) {
        error = "unknown module " + moduleId;
        return false;
    }
    if ((moduleId == "amp.nam" || moduleId == "cab.ir") && assetFile.empty()) {
        error = "select an asset (model / IR) for " + moduleId;
        return false;
    }
    if (!assetFile.empty() && !std::ifstream(assetFile).good()) {
        error = "asset file not found: " + assetFile;
        return false;
    }
    std::vector<audio::SlotDef> slots = audio::snapshotChain(*chain_);
    audio::SlotDef def;
    def.category = registry_->categoryOf(moduleId);
    def.impl = implForModule(moduleId);
    def.moduleId = moduleId;
    def.file = assetFile;
    for (const ParamSpec& spec : registry_->specsFor(moduleId)) {
        def.params.push_back(ParamInit{spec.id, spec.defaultValue});
    }
    int nextSlot = 0;
    for (const audio::SlotDef& s : slots) {
        nextSlot = std::max(nextSlot, s.slot + 1);
    }
    def.slot = nextSlot;
    slots.push_back(std::move(def));
    return rebuildChain(std::move(slots), error);
}

bool EngineHost::insertModuleToChain(int atIndex, const std::string& moduleId,
                                     const std::string& assetFile, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    if (chain_->slotCount() >= kMaxChainSlots) {
        error = "chain limit reached (12 slots)";
        return false;
    }
    if (!registry_->has(moduleId)) {
        error = "unknown module " + moduleId;
        return false;
    }
    if ((moduleId == "amp.nam" || moduleId == "cab.ir") && assetFile.empty()) {
        error = "select an asset (model / IR) for " + moduleId;
        return false;
    }
    if (!assetFile.empty() && !std::ifstream(assetFile).good()) {
        error = "asset file not found: " + assetFile;
        return false;
    }
    std::vector<audio::SlotDef> slots = audio::snapshotChain(*chain_);
    audio::SlotDef def;
    def.category = registry_->categoryOf(moduleId);
    def.impl = implForModule(moduleId);
    def.moduleId = moduleId;
    def.file = assetFile;
    for (const ParamSpec& spec : registry_->specsFor(moduleId)) {
        def.params.push_back(ParamInit{spec.id, spec.defaultValue});
    }
    const int at = std::min(std::max(atIndex, 0), static_cast<int>(slots.size()));
    slots.insert(slots.begin() + at, std::move(def));
    for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i].slot = static_cast<int>(i);
    }
    return rebuildChain(std::move(slots), error);
}

bool EngineHost::removeModuleFromChain(int slot, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    std::vector<audio::SlotDef> slots = audio::snapshotChain(*chain_);
    bool found = false;
    for (auto it = slots.begin(); it != slots.end(); ++it) {
        if (it->slot == slot) {
            slots.erase(it);
            found = true;
            break;
        }
    }
    if (!found) {
        error = "no slot " + std::to_string(slot);
        return false;
    }
    return rebuildChain(std::move(slots), error);
}

bool EngineHost::moveModuleTo(int slot, int dstIndex, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    std::vector<audio::SlotDef> slots = audio::snapshotChain(*chain_);
    bool ok = false;
    std::vector<audio::SlotDef> reordered = audio::reorderChain(slots, slot, dstIndex, ok);
    if (!ok) {
        error = "cannot reorder slot " + std::to_string(slot);
        return false;
    }
    return rebuildChain(std::move(reordered), error);
}

bool EngineHost::swapModules(int slotA, int slotB, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    std::vector<audio::SlotDef> slots = audio::snapshotChain(*chain_);
    int a = -1;
    int b = -1;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].slot == slotA) a = static_cast<int>(i);
        if (slots[i].slot == slotB) b = static_cast<int>(i);
    }
    if (a < 0 || b < 0 || a == b) {
        error = "cannot swap slots";
        return false;
    }
    std::swap(slots[static_cast<std::size_t>(a)], slots[static_cast<std::size_t>(b)]);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i].slot = static_cast<int>(i);
    }
    return rebuildChain(std::move(slots), error);
}

bool EngineHost::moveModule(int slot, int direction, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    int index = -1;
    for (int i = 0; i < chain_->slotCount(); ++i) {
        try {
            if (chain_->defOf(i).slot == slot) {
                index = i;
                break;
            }
        } catch (const std::out_of_range&) {
        }
    }
    if (index < 0) {
        error = "no slot " + std::to_string(slot);
        return false;
    }
    const int dst = index + direction;
    if (dst < 0 || dst >= chain_->slotCount()) {
        error = "already at the edge";
        return false;
    }
    std::vector<audio::SlotDef> slots = audio::snapshotChain(*chain_);
    bool ok = false;
    std::vector<audio::SlotDef> reordered = audio::reorderChain(slots, slot, dst, ok);
    if (!ok) {
        error = "cannot reorder slot " + std::to_string(slot);
        return false;
    }
    return rebuildChain(std::move(reordered), error);
}

bool EngineHost::savePreset(const std::string& name, const std::string& dir, std::string& error)
{
    if (name.empty() || name.find_first_of("/\\") != std::string::npos) {
        error = "invalid preset name";
        return false;
    }
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    preset::Preset preset;
    preset.name = name;
    preset.chain = audio::snapshotChain(*chain_);
    preset.scenes = scenesDefs_; // the current scene bank rides along
    const std::string json = preset::savePreset(preset);
    const std::string path = dir + "/" + name + ".json";
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    f << json;
    f.close();
    if (!f) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

std::string EngineHost::exportPresetJson(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        return {};
    }
    preset::Preset preset;
    preset.name = name.empty() ? "preset" : name;
    preset.chain = audio::snapshotChain(*chain_);
    preset.scenes = scenesDefs_;
    return preset::savePreset(preset);
}

std::vector<std::string> EngineHost::moduleIds() const
{
    return registry_->allIds();
}

std::vector<EngineHost::SlotInfo> EngineHost::chainInfo() const
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    std::vector<SlotInfo> out;
    if (chain_ == nullptr) {
        return out;
    }
    for (int i = 0; i < chain_->slotCount(); ++i) {
        SlotInfo info;
        try {
            const audio::SlotDef def = chain_->defOf(i);
            info.slot = def.slot;
            info.moduleId = def.moduleId;
            // asset display name: file basename without extension
            const std::string& f = def.file;
            const std::size_t slash = f.find_last_of("/\\");
            const std::string base = slash == std::string::npos ? f : f.substr(slash + 1);
            const std::size_t dot = base.find_last_of('.');
            info.assetName = dot == std::string::npos ? base : base.substr(0, dot);
            info.bypass = def.bypass;
            info.mix = chain_->mixValueOf(i);
            info.specs = chain_->specsOf(i);
            for (std::size_t p = 0; p < info.specs.size(); ++p) {
                info.values.push_back(chain_->paramValue(i, p));
            }
        } catch (const std::out_of_range&) {
            continue;
        }
        out.push_back(std::move(info));
    }
    return out;
}

void EngineHost::handleMidi(const midi::Event& event)
{
    midi_.handleEvent(event, router_, scenes_, midiActions_);
}

bool EngineHost::midiLearnParam(int cc, const std::string& moduleId, const std::string& paramId,
                                std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (chain_ == nullptr) {
        error = "no chain loaded";
        return false;
    }
    if (cc < 0 || cc > 127) {
        error = "CC out of range";
        return false;
    }
    if (!midi_.learnBind(router_, *chain_, cc, moduleId, paramId)) {
        error = "binding rejected (CC in use or unknown param)";
        return false;
    }
    return true;
}

bool EngineHost::midiLearnScene(int cc, int sceneIndex, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (cc < 0 || cc > 127 || sceneIndex < 1 || sceneIndex > audio::SceneEngine::kMaxScenes) {
        error = "CC / scene out of range";
        return false;
    }
    if (!midi_.bindScene(cc, sceneIndex)) {
        error = "binding rejected (CC in use)";
        return false;
    }
    return true;
}

void EngineHost::midiClearBind(int cc)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    midi_.clearBind(cc);
    midi_.clearScene(cc);
}

std::vector<midi::MidiRouter::BindInfo> EngineHost::midiBindings() const
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    return midi_.bindings();
}

bool EngineHost::midiRestoreBind(const midi::MidiRouter::BindInfo& bind, std::string& error)
{
    std::lock_guard<std::mutex> lock(chainMutex_);
    if (bind.kind == midi::MidiRouter::BindInfo::Kind::Param) {
        if (chain_ == nullptr) {
            error = "no chain loaded";
            return false;
        }
        if (!midi_.learnBind(router_, *chain_, bind.cc, bind.moduleId, bind.paramId)) {
            error = "cannot restore param bind CC " + std::to_string(bind.cc);
            return false;
        }
        return true;
    }
    if (!midi_.bindScene(bind.cc, bind.sceneIndex)) {
        error = "cannot restore scene bind CC " + std::to_string(bind.cc);
        return false;
    }
    return true;
}

} // namespace desktop
} // namespace namfx
