#include "memtable.hpp"

#include <benchmark/benchmark.h>

#include <random>

using namespace mousedb::memtable;

static void memtable_KVStoreInsertSmall(benchmark::State &state) {
    KVStore kvs(4096);
    std::string key("key");
    std::string value("value");
    std::span<std::byte> key_span(reinterpret_cast<std::byte *>(key.data()),
                                  key.size());
    std::span<std::byte> value_span(reinterpret_cast<std::byte *>(value.data()),
                                    value.size());
    for (auto _ : state) {
        auto ptr = kvs.insert(key_span, value_span);
        benchmark::DoNotOptimize(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}

static void memtable_KVStoreInsertHuge(benchmark::State &state) {
    KVStore kvs(1 << 20);
    std::string key("key");
    std::string value(4096, 'v');
    std::span<std::byte> key_span(reinterpret_cast<std::byte *>(key.data()),
                                  key.size());
    std::span<std::byte> value_span(reinterpret_cast<std::byte *>(value.data()),
                                    value.size());
    for (auto _ : state) {
        auto ptr = kvs.insert(key_span, value_span);
        benchmark::DoNotOptimize(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}

static void memtable_KVStoreInsertRandom(benchmark::State &state) {
    KVStore kvs(1 << 20);

    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(4, 4096);

    std::string key = "key";
    std::span<std::byte> key_span(reinterpret_cast<std::byte *>(key.data()),
                                  key.size());

    for (auto _ : state) {
        size_t size = dist(rng);
        std::string value(size, 'v');
        std::span<std::byte> value_span(
            reinterpret_cast<std::byte *>(value.data()), size);
        auto ptr = kvs.insert(key_span, value_span);
        benchmark::DoNotOptimize(ptr);
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(memtable_KVStoreInsertSmall)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(memtable_KVStoreInsertHuge)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();

BENCHMARK(memtable_KVStoreInsertRandom)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->UseRealTime();
