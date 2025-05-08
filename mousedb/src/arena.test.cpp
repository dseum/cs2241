#include "arena.hpp"

#include <gtest/gtest.h>

#include <mutex>
#include <thread>

using namespace mousedb::arena;

TEST(arena_Arena, AllocateAndUnused) {
    Arena arena(4096);
    auto p1 = arena.allocate(100);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(arena.unused(), 4096u - 100u);
}

TEST(arena_Arena, MultipleAllocations) {
    Arena arena(4096);
    auto p1 = arena.allocate(100);
    auto p2 = arena.allocate(200);
    EXPECT_EQ(p2 - p1, 100);
    EXPECT_EQ(arena.unused(), 4096u - 100u - 200u);
}

TEST(arena_Arena, AllocateExactSlabSize) {
    Arena arena(4096);
    auto p = arena.allocate(4096);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(arena.unused(), 0u);
}

TEST(arena_Arena, AllocateSlabUniqueness) {
    Arena arena(4096);
    auto s1 = arena.allocate_slab(100);
    auto s2 = arena.allocate_slab(200);
    EXPECT_NE(s1, s2);
}

TEST(arena_ConcurrentArena, AllocateAndUnused) {
    ConcurrentArena arena(4096);
    auto p = arena.allocate(100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(arena.unused(), 4096u - 100u);
}

TEST(arena_ConcurrentArena, MultipleAllocations) {
    ConcurrentArena arena(4096);
    auto p1 = arena.allocate(100);
    auto p2 = arena.allocate(200);
    EXPECT_EQ(p2 - p1, 100);
    EXPECT_EQ(arena.unused(), 4096u - 100u - 200u);
}

TEST(arena_ConcurrentArena, LargeAllocation) {
    ConcurrentArena arena(4096);
    auto before = arena.unused();
    // requests larger than slab_size, should use a fresh slab but not
    // change unused
    auto p = arena.allocate(5000);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(arena.unused(), before);
}

TEST(arena_ConcurrentArena, ThreadSafety) {
    ConcurrentArena arena(4096);
    std::vector<ConcurrentArena::ptr_type> ptrs;
    std::mutex ptrs_mutex;

    auto worker = [&]() {
        auto p = arena.allocate(200);
        std::scoped_lock<std::mutex> lock(ptrs_mutex);
        ptrs.push_back(p);
    };

    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();

    // both threads should have allocated non-null, distinct pointers
    EXPECT_EQ(ptrs.size(), 2u);
    EXPECT_NE(ptrs[0], ptrs[1]);
    // total unused should reflect both allocations
    EXPECT_EQ(arena.unused(), 4096u - 200u * 2u);
}

TEST(arena_ConcurrentArena, ExtremeTripleAlternatingSizes) {
    constexpr size_t slab_size = 4096;
    ConcurrentArena arena(slab_size);

    constexpr size_t num_threads = 8;
    constexpr size_t allocs_per_thread = 3000;
    constexpr size_t total_allocs = num_threads * allocs_per_thread;

    constexpr size_t size_a = 64;    // small
    constexpr size_t size_b = 128;   // medium
    constexpr size_t size_c = 1024;  // large

    std::vector<ConcurrentArena::ptr_type> ptrs;
    ptrs.reserve(total_allocs);
    std::mutex ptrs_mutex;

    auto worker = [&]() {
        for (size_t i = 0; i < allocs_per_thread; ++i) {
            size_t sz = (i % 3 == 0 ? size_a : i % 3 == 1 ? size_b : size_c);
            auto p = arena.allocate(sz);
            EXPECT_NE(p, nullptr);

            std::scoped_lock lock(ptrs_mutex);
            ptrs.push_back(p);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker);
    }
    for (auto &th : threads) th.join();

    // all allocations recorded
    ASSERT_EQ(ptrs.size(), total_allocs);

    // no duplicates
    std::sort(ptrs.begin(), ptrs.end());
    auto uniq_end = std::unique(ptrs.begin(), ptrs.end());
    EXPECT_EQ(std::distance(ptrs.begin(), uniq_end), total_allocs);
}
