#pragma once

#include <atomic>

namespace mousedb {
namespace spin_mutex {
class SpinMutex {
   public:
    SpinMutex() = default;
    SpinMutex(const SpinMutex &) = delete;
    SpinMutex &operator=(const SpinMutex &) = delete;

    auto lock() -> void {
        while (flag_.test_and_set(std::memory_order_acquire));
    }

    auto try_lock() -> bool {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

    auto unlock() -> void {
        flag_.clear(std::memory_order_release);
    }

   private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
}  // namespace spin_mutex
}  // namespace mousedb
