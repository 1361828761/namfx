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
// resampling when the engine rate differs), and exposes the PLAN §6 limited
// tone controls: gain (pre), bass/middle/treble (post EQ), output (post
// level). All NAM types are confined to nam_amp.cpp (C++20); this header is
// plain C++17.
class NamAmpModule : public ModuleBase {
public:
    ~NamAmpModule() override;

    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }

    bool loadAsset(const std::string& path) override;
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    void setParameter(const std::string& id, float value) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void registerNamAmp(ModuleRegistry& registry);

} // namespace namfx
