#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace namfx {
namespace audio {

class AudioGraph {
public:
    void processBlock(const float* in, float* out, std::size_t numSamples) noexcept;

    void commit() noexcept;
    void swap() noexcept;

    std::uint32_t front() const noexcept { return front_.load(std::memory_order_acquire); }

private:
    std::atomic<std::uint32_t> front_{0};
    std::atomic<bool> swap_requested_{false};
};

} // namespace audio
} // namespace namfx
