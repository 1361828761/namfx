#pragma once

#include <atomic>
#include <memory>

namespace namfx {

class ModuleRegistry;

namespace audio {

class Chain;

class AudioGraph {
public:
    AudioGraph();
    ~AudioGraph();
    AudioGraph(const AudioGraph&) = delete;
    AudioGraph& operator=(const AudioGraph&) = delete;

    void setRegistry(std::shared_ptr<const ModuleRegistry> registry);

    void requestSwap(std::unique_ptr<Chain> next);
    void processBlock(const float* inL, const float* inR, float* outL, float* outR, int n);
    bool hasPending() const;

    // control-thread read of the live chain (null when none loaded); the
    // pointer stays valid until the next swap lands, callers must re-fetch
    // after requestSwap
    const Chain* current() const;

    // audio-thread read of the live chain, after processBlock() has landed
    // any pending swap for this block (null when none loaded): the chain
    // the scene engine / control router apply to at the block boundary
    Chain* live() const;

private:
    std::shared_ptr<const ModuleRegistry> registry_;
    std::atomic<Chain*> pending_{nullptr};
    std::atomic<Chain*> retired_{nullptr};
    std::unique_ptr<Chain> slots_[2];
    std::atomic<int> current_{0};
};

} // namespace audio
} // namespace namfx
