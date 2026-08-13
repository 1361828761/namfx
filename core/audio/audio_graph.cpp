#include "audio/audio_graph.h"

namespace namfx {
namespace audio {

void AudioGraph::processBlock(const float* in, float* out, std::size_t numSamples) noexcept
{
    std::memcpy(out, in, numSamples * sizeof(float));
}

void AudioGraph::commit() noexcept
{
    swap_requested_.store(true, std::memory_order_release);
}

void AudioGraph::swap() noexcept
{
    bool expected = true;
    if (swap_requested_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        const std::uint32_t current = front_.load(std::memory_order_relaxed);
        front_.store(current ^ 1u, std::memory_order_release);
    }
}

} // namespace audio
} // namespace namfx
