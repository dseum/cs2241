#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include "spin_mutex.hpp"

namespace mousedb {
namespace arena {
class Arena {
   public:
    using ptr_type = std::byte *;

    explicit Arena(size_t slab_size);
    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    // In bytes, the amount of used memory in the arena.
    auto used() const -> size_t;
    // In bytes, the amount of unused memory in the arena. This includes
    // unused memory from inactive slabs.
    auto unused() const -> size_t;
    // In bytes, the total size of the arena.
    auto size() const -> size_t;

    auto allocate(size_t size) -> ptr_type;
    auto allocate_slab(size_t size) -> ptr_type;

   private:
    const size_t slab_size_;
    std::deque<std::unique_ptr<std::byte[]>> slabs_;
    ptr_type active_slab_ = nullptr;
    size_t active_slab_unused_ = 0;
    size_t used_ = 0;
    size_t unused_ = 0;
    size_t size_ = 0;

    // Creates a slab of size
    auto slabify(size_t size) -> ptr_type;
};

class ConcurrentArena {
   public:
    using ptr_type = std::byte *;

    explicit ConcurrentArena(size_t slab_size);
    ConcurrentArena(const ConcurrentArena &) = delete;
    ConcurrentArena &operator=(const ConcurrentArena &) = delete;

    // In bytes, the amount of used memory in the arena.
    auto used() const -> size_t;
    // In bytes, the amount of unused memory in the arena. This includes
    // unused memory from inactive slabs.
    auto unused() const -> size_t;
    // In bytes, the total size of the arena.
    auto size() const -> size_t;

    auto allocate(size_t size) -> ptr_type;

   private:
    struct alignas(64) Shard {
        ptr_type begin = nullptr;
        std::atomic<size_t> unused = 0;
        spin_mutex::SpinMutex mutex;
    };

    static thread_local size_t cpu_id_;

    const size_t num_cpus_;
    const size_t slab_size_;
    const size_t slab_slice_size_;

    Arena arena_;
    spin_mutex::SpinMutex arena_mutex_;
    std::vector<Shard> shards_;
    std::atomic<size_t> used_ = 0;
    std::atomic<size_t> unused_ = 0;
    std::atomic<size_t> size_ = 0;

    inline auto update() -> void;
    inline auto get_shard(size_t cpu_id) const -> Shard *;
    inline auto reset_shard() -> Shard *;
};
}  // namespace arena
}  // namespace mousedb
