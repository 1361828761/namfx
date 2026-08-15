#include "audio/audio_graph.h"

#include "audio/chain.h"

#include <cstring>

namespace namfx {
namespace audio {

AudioGraph::AudioGraph() = default;

AudioGraph::~AudioGraph()
{
    delete pending_.exchange(nullptr, std::memory_order_acq_rel);
    delete retired_.exchange(nullptr, std::memory_order_acq_rel);
}

void AudioGraph::setRegistry(std::shared_ptr<const ModuleRegistry> registry)
{
    registry_ = std::move(registry);
}

void AudioGraph::requestSwap(std::unique_ptr<Chain> next)
{
    delete retired_.exchange(nullptr, std::memory_order_acq_rel);
    Chain* previous = pending_.exchange(next.release(), std::memory_order_acq_rel);
    delete previous;
}

void AudioGraph::processBlock(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    Chain* incoming = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (incoming != nullptr) {
        const int current = current_.load(std::memory_order_relaxed);
        Chain* displaced = slots_[current ^ 1].release();
        slots_[current ^ 1].reset(incoming);
        Chain* leftover = retired_.exchange(displaced, std::memory_order_acq_rel);
        delete leftover;
        current_.store(current ^ 1, std::memory_order_release);
    }
    const int live = current_.load(std::memory_order_acquire);
    if (slots_[live] != nullptr) {
        slots_[live]->process(inL, inR, outL, outR, n);
        return;
    }
    std::memcpy(outL, inL, static_cast<std::size_t>(n) * sizeof(float));
    std::memcpy(outR, inR, static_cast<std::size_t>(n) * sizeof(float));
}

bool AudioGraph::hasPending() const
{
    return pending_.load(std::memory_order_acquire) != nullptr;
}

const Chain* AudioGraph::current() const
{
    return slots_[current_.load(std::memory_order_acquire)].get();
}

Chain* AudioGraph::live() const
{
    return slots_[current_.load(std::memory_order_acquire)].get();
}

} // namespace audio
} // namespace namfx
