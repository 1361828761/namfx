#include "platform/rt_alloc.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("allocation outside audio callback is allowed")
{
    namfx::rt::AllocCounter::reset_violation();

    int* p = new int[16];
    delete[] p;

    std::vector<float> v(1024);
    REQUIRE(v.size() == 1024);

    REQUIRE_FALSE(namfx::rt::AllocCounter::violation);
#ifdef NAMFX_RT_ALLOC_ENABLED
    REQUIRE(namfx::rt::AllocCounter::total.load() > 0);
#endif
}

#ifdef NAMFX_RT_ALLOC_ENABLED
TEST_CASE("allocation inside audio callback is detected")
{
    namfx::rt::ScopedAllocGuard guard;

    std::vector<float> v(1024);
    REQUIRE(v.size() == 1024);

    REQUIRE(guard.violated());
}

TEST_CASE("scoped guard clears callback flag on destruction")
{
    {
        namfx::rt::ScopedAllocGuard guard;
        REQUIRE_FALSE(guard.violated());
    }

    namfx::rt::AllocCounter::reset_violation();
    std::vector<float> v(64);
    REQUIRE(v.size() == 64);
    REQUIRE_FALSE(namfx::rt::AllocCounter::violation);
}
#endif
