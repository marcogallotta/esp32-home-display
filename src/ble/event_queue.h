#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "scanner.h"

namespace ble {

// SPSC lock-free ring buffer. push() is called from the BLE thread;
// pop() is called from the main thread.
class EventQueue {
    static constexpr std::size_t kCapacity = 32;
    static constexpr std::size_t kMask = kCapacity - 1;

    std::array<AdvertisementEvent, kCapacity> buffer_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};

public:
    bool push(const AdvertisementEvent& event) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & kMask;
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[tail] = event;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(AdvertisementEvent& out) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        out = buffer_[head];
        head_.store((head + 1) & kMask, std::memory_order_release);
        return true;
    }
};

} // namespace ble
