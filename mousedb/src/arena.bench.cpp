#include "arena.hpp"

#include <benchmark/benchmark.h>

#include <cstdlib>

using namespace mousedb::arena;

static void arena_BasicMalloc(benchmark::State &state) {
    const size_t size = state.range(0);
    for (auto _ : state) {
        auto ptr = reinterpret_cast<std::byte *>(malloc(size));
        benchmark::DoNotOptimize(ptr);
        free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}

static void arena_BasicNew(benchmark::State &state) {
    const size_t size = state.range(0);
    for (auto _ : state) {
        auto ptr = new std::byte[size];
        benchmark::DoNotOptimize(ptr);
        delete[] ptr;
    }
    state.SetItemsProcessed(state.iterations());
}

static void arena_ArenaAllocate(benchmark::State &state) {
    const size_t size = state.range(0);
    Arena arena(1 << 20);
    for (auto _ : state) {
        auto ptr = arena.allocate(size);
        benchmark::DoNotOptimize(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}

static void arena_ConcurrentArenaAllocate(benchmark::State &state) {
    const size_t size = state.range(0);
    ConcurrentArena arena(1 << 20);
    for (auto _ : state) {
        auto ptr = arena.allocate(size);
        benchmark::DoNotOptimize(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(arena_BasicMalloc)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(arena_BasicNew)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(arena_ArenaAllocate)->Arg(64)->Arg(256)->Arg(1024)->Arg(4096);

BENCHMARK(arena_ConcurrentArenaAllocate)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();
