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

private:
    std::shared_ptr<const ModuleRegistry> registry_;
    std::atomic<Chain*> pending_{nullptr};
    std::atomic<Chain*> retired_{nullptr};
    std::unique_ptr<Chain> slots_[2];
    std::atomic<int> current_{0};
};

} // namespace audio
} // namespace namfx
