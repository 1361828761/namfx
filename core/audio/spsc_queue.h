#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

namespace namfx {

template <typename T, size_t Capacity>
class SpscQueue {
    static_assert(Capacity > 0, "capacity must be positive");
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    SpscQueue() = default;

    bool push(const T& item)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail - head >= Capacity) {
            return false;
        }
        data_[tail & (Capacity - 1)] = item;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (tail == head) {
            return false;
        }
        out = std::move(data_[head & (Capacity - 1)]);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    std::size_t size() const
    {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return tail - head;
    }

    static constexpr std::size_t capacity = Capacity;

private:
    std::array<T, Capacity> data_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

} // namespace namfx
