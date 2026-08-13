#include "platform/rt_alloc.h"

#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#endif

#include <cstdio>

namespace namfx {
namespace rt {

#ifdef NAMFX_RT_ALLOC_ENABLED

namespace {

void* aligned_allocate(std::size_t size, std::size_t alignment)
{
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    void* p = nullptr;
    if (posix_memalign(&p, alignment, size) != 0) {
        return nullptr;
    }
    return p;
#endif
}

void aligned_deallocate(void* p) noexcept
{
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}

} // namespace

#endif // NAMFX_RT_ALLOC_ENABLED

std::atomic<std::uint64_t> AllocCounter::total{0};
thread_local bool AllocCounter::in_audio_callback = false;
thread_local bool AllocCounter::violation = false;

#ifdef NAMFX_RT_ALLOC_ENABLED
namespace {
std::atomic<std::uint64_t> oom_failpoint{0};
}

void AllocCounter::armOomFailpoint(std::uint64_t afterN) noexcept
{
    oom_failpoint.store(afterN, std::memory_order_relaxed);
}

void AllocCounter::disarmOomFailpoint() noexcept
{
    oom_failpoint.store(0, std::memory_order_relaxed);
}

bool AllocCounter::shouldFailAllocation() noexcept
{
    std::uint64_t remaining = oom_failpoint.load(std::memory_order_relaxed);
    while (remaining > 0) {
        if (oom_failpoint.compare_exchange_weak(remaining, remaining - 1,
                                                std::memory_order_relaxed)) {
            if (remaining == 1) {
                // single-shot: disarm on failure so exception unwinding does
                // not cascade into further allocation failures (terminate)
                oom_failpoint.store(0, std::memory_order_relaxed);
                return true;
            }
            return false;
        }
    }
    return false;
}
#endif

#ifdef NAMFX_RT_ALLOC_ENABLED
namespace {
thread_local bool fail_log_enabled = false;
}
void AllocCounter::enableFailLog(bool on) noexcept
{
    fail_log_enabled = on;
}
void AllocCounter::logFail(const char* kind, std::size_t size) noexcept
{
    if (fail_log_enabled) {
        std::printf("OOM-FAIL %s size=%zu total=%llu\n", kind, size,
                    static_cast<unsigned long long>(AllocCounter::total.load()));
        std::fflush(stdout);
    }
}
#endif

void AllocCounter::record_allocation() noexcept
{
#ifdef NAMFX_RT_ALLOC_ENABLED
    total.fetch_add(1, std::memory_order_relaxed);
    if (AllocCounter::in_audio_callback) {
        AllocCounter::violation = true;
    }
#endif
}

void AllocCounter::reset_violation() noexcept
{
    AllocCounter::violation = false;
}

ScopedAllocGuard::ScopedAllocGuard() noexcept
{
#ifdef NAMFX_RT_ALLOC_ENABLED
    prev_in_audio_callback_ = AllocCounter::in_audio_callback;
    prev_violation_ = AllocCounter::violation;
    AllocCounter::in_audio_callback = true;
#endif
}

ScopedAllocGuard::~ScopedAllocGuard() noexcept
{
#ifdef NAMFX_RT_ALLOC_ENABLED
    AllocCounter::in_audio_callback = prev_in_audio_callback_;
    AllocCounter::violation = prev_violation_;
#endif
}

bool ScopedAllocGuard::violated() const noexcept
{
#ifdef NAMFX_RT_ALLOC_ENABLED
    return AllocCounter::violation && !prev_violation_;
#else
    return false;
#endif
}

} // namespace rt
} // namespace namfx

#ifdef NAMFX_RT_ALLOC_ENABLED

void* operator new(std::size_t size)
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("new", size);
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("new[]", size);
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("nothrow", size);
        return nullptr;
    }
    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("nothrow[]", size);
        return nullptr;
    }
    return std::malloc(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("aligned", size);
        throw std::bad_alloc();
    }
    if (void* p = namfx::rt::aligned_allocate(size, static_cast<std::size_t>(alignment))) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("aligned[]", size);
        throw std::bad_alloc();
    }
    if (void* p = namfx::rt::aligned_allocate(size, static_cast<std::size_t>(alignment))) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("aligned-nothrow", size);
        return nullptr;
    }
    return namfx::rt::aligned_allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    namfx::rt::AllocCounter::record_allocation();
    if (namfx::rt::AllocCounter::shouldFailAllocation()) {
        namfx::rt::AllocCounter::logFail("aligned-nothrow[]", size);
        return nullptr;
    }
    return namfx::rt::aligned_allocate(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t, const std::nothrow_t&) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t, const std::nothrow_t&) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::align_val_t) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

void operator delete[](void* p, std::align_val_t) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

void operator delete(void* p, std::size_t, std::align_val_t, const std::nothrow_t&) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t, const std::nothrow_t&) noexcept
{
    namfx::rt::aligned_deallocate(p);
}

#endif // NAMFX_RT_ALLOC_ENABLED
