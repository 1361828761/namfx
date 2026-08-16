#pragma once

#include "modules/module_base.h"
#include "modules/module_registry.h"

#include <memory>
#include <string>
#include <vector>

namespace namfx {

// NAM amp module (black-box neural amp model, official NAM Core wrapped as
// an isolated static library; docs/research/nam_integration.md). Loads a
// .nam file, runs the model at its expected sample rate (internal streaming
// resampling when the engine rate differs), and exposes the PLAN sec 6
// limited tone controls: gain (pre), bass/middle/treble (post EQ), output
// (post level), tier (A2 slimmable size). All NAM types are confined to
// nam_amp.cpp (C++20); this header is plain C++17.
class NamAmpModule : public ModuleBase {
public:
    ~NamAmpModule() override;

    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }

    bool loadAsset(const std::string& path) override;
    bool loadAssetBytes(const std::uint8_t* data, std::size_t size) override;
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    void setParameter(const std::string& id, float value) override;

    // control-thread hook: Chain::prepare calls this after pushing the
    // parameter targets, so a preset `tier` takes effect on load
    void applyAssetOptions() override { applyTier(); }

    // A2 tier switching (slimmable models): applies the `tier` parameter
    // target (0 = Lite/minimal, 1 = Full). CONTROL THREAD ONLY - NAM Core's
    // SetSlimmableSize takes a mutex and resets the submodel, never call
    // from the audio callback. reset() re-applies it automatically.
    void applyTier();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void registerNamAmp(ModuleRegistry& registry);

} // namespace namfx
