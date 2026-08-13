#include "platform/rt_alloc.h"

#include <cstdlib>
#include <new>

namespace namfx {
namespace rt {

std::atomic<std::uint64_t> AllocCounter::total{0};
thread_local bool AllocCounter::in_audio_callback = false;
thread_local bool AllocCounter::violation = false;

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
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    namfx::rt::AllocCounter::record_allocation();
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    namfx::rt::AllocCounter::record_allocation();
    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    namfx::rt::AllocCounter::record_allocation();
    return std::malloc(size);
}

void* operator new(std::size_t size, std::align_val_t)
{
    namfx::rt::AllocCounter::record_allocation();
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t)
{
    namfx::rt::AllocCounter::record_allocation();
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
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
    std::free(p);
}

void operator delete[](void* p, std::align_val_t) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    std::free(p);
}

#endif // NAMFX_RT_ALLOC_ENABLED
