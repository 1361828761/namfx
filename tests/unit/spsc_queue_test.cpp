#include "audio/spsc_queue.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

TEST_CASE("spsc queue preserves push and pop order")
{
    namfx::SpscQueue<int, 8> q;
    for (int i = 0; i < 5; ++i) {
        REQUIRE(q.push(i));
    }
    for (int i = 0; i < 5; ++i) {
        int out = -1;
        REQUIRE(q.pop(out));
        REQUIRE(out == i);
    }
    int out = 0;
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("spsc queue pop on empty returns false and leaves output untouched")
{
    namfx::SpscQueue<int, 4> q;
    int out = 42;
    REQUIRE_FALSE(q.pop(out));
    REQUIRE(out == 42);
}

TEST_CASE("spsc queue push beyond capacity returns false without corrupting contents")
{
    namfx::SpscQueue<int, 4> q;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(q.push(i));
    }
    REQUIRE(q.size() == 4);
    REQUIRE_FALSE(q.push(99));

    for (int i = 0; i < 4; ++i) {
        int out = -1;
        REQUIRE(q.pop(out));
        REQUIRE(out == i);
    }
    int out = 0;
    REQUIRE_FALSE(q.pop(out));
    REQUIRE(q.size() == 0);
}

TEST_CASE("spsc queue handles a single item")
{
    namfx::SpscQueue<int, 2> q;
    REQUIRE(q.push(7));
    REQUIRE(q.size() == 1);

    int out = 0;
    REQUIRE(q.pop(out));
    REQUIRE(out == 7);
    REQUIRE(q.size() == 0);
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("spsc queue interleaves 100k items preserving order")
{
    namfx::SpscQueue<int, 256> q;
    for (int i = 0; i < 100000; ++i) {
        REQUIRE(q.push(i));
        int out = -1;
        REQUIRE(q.pop(out));
        REQUIRE(out == i);
    }
    REQUIRE(q.size() == 0);
}

TEST_CASE("spsc queue wraps the ring with bursts")
{
    namfx::SpscQueue<int, 16> q;
    int expected = 0;
    for (int burst = 0; burst < 1000; ++burst) {
        for (int i = 0; i < 10; ++i) {
            REQUIRE(q.push(expected));
            ++expected;
        }
        for (int i = 0; i < 10; ++i) {
            int out = -1;
            REQUIRE(q.pop(out));
            REQUIRE(out == expected - 10 + i);
        }
    }
    REQUIRE(q.size() == 0);
}

TEST_CASE("spsc queue refills after partial drain")
{
    namfx::SpscQueue<int, 16> q;
    for (int i = 0; i < 16; ++i) {
        REQUIRE(q.push(i));
    }
    REQUIRE_FALSE(q.push(100));

    int out = -1;
    REQUIRE(q.pop(out));
    REQUIRE(out == 0);

    REQUIRE(q.push(100));

    for (int i = 0; i < 16; ++i) {
        REQUIRE(q.pop(out));
    }
    REQUIRE(out == 100);
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("spsc queue works with pair type")
{
    namfx::SpscQueue<std::pair<int, float>, 8> q;
    REQUIRE(q.push({1, 2.5f}));
    REQUIRE(q.push({2, 3.5f}));

    std::pair<int, float> out{0, 0.0f};
    REQUIRE(q.pop(out));
    REQUIRE(out == std::make_pair(1, 2.5f));
    REQUIRE(q.pop(out));
    REQUIRE(out == std::make_pair(2, 3.5f));
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("spsc queue capacity is exposed as power of two")
{
    namfx::SpscQueue<int, 64> q;
    REQUIRE(namfx::SpscQueue<int, 64>::capacity == 64);
    REQUIRE(q.capacity == 64);
    REQUIRE(q.size() == 0);
}

TEST_CASE("spsc queue supports concurrent single producer single consumer")
{
    namfx::SpscQueue<int, 1024> q;
    const int total = 100000;
    std::vector<int> received;
    received.reserve(static_cast<std::size_t>(total));

    std::thread producer([&q, total] {
        for (int i = 0; i < total; ++i) {
            while (!q.push(i)) {
                std::this_thread::yield();
            }
        }
    });
    std::thread consumer([&q, &received, total] {
        while (received.size() < static_cast<std::size_t>(total)) {
            int out = -1;
            if (q.pop(out)) {
                received.push_back(out);
            }
        }
    });
    producer.join();
    consumer.join();

    for (int i = 0; i < total; ++i) {
        INFO("item " << i);
        REQUIRE(received[static_cast<std::size_t>(i)] == i);
    }
}
