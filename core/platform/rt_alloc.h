#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef NDEBUG
#define NAMFX_RT_ALLOC_ENABLED 1
#endif

namespace namfx {
namespace rt {

struct AllocCounter {
    static std::atomic<std::uint64_t> total;
    static thread_local bool in_audio_callback;
    static thread_local bool violation;

    static void record_allocation() noexcept;
    static void reset_violation() noexcept;

#ifdef NAMFX_RT_ALLOC_ENABLED
    // test-only OOM failpoint: armed with N, the Nth subsequent allocation
    // fails (operator new throws bad_alloc / nothrow forms return nullptr)
    static void armOomFailpoint(std::uint64_t afterN) noexcept;
    static void disarmOomFailpoint() noexcept;
    static bool shouldFailAllocation() noexcept;
    static void enableFailLog(bool on) noexcept;
    static void logFail(const char* kind, std::size_t size) noexcept;
#endif
};

class ScopedAllocGuard {
public:
    ScopedAllocGuard() noexcept;
    ~ScopedAllocGuard() noexcept;
    ScopedAllocGuard(const ScopedAllocGuard&) = delete;
    ScopedAllocGuard& operator=(const ScopedAllocGuard&) = delete;

    bool violated() const noexcept;

private:
#ifdef NAMFX_RT_ALLOC_ENABLED
    bool prev_in_audio_callback_;
    bool prev_violation_;
#endif
};

} // namespace rt
} // namespace namfx
