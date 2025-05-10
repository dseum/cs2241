#include "filter.hpp"

#include <benchmark/benchmark.h>

#include <random>
#include <string>
#include <vector>

std::discrete_distribution<size_t> make_zipf(size_t K, double s) {
    std::vector<double> w(K);
    for (size_t i = 0; i < K; ++i) {
        w[i] = 1.0 / std::pow(i + 1, s);
    }
    return std::discrete_distribution<size_t>(w.begin(), w.end());
}

using namespace mousedb::filter;

static std::vector<std::string> make_random_strings(size_t n) {
    std::vector<std::string> v;
    v.reserve(n);
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<uint64_t> dist;
    for (size_t i = 0; i < n; ++i) {
        v.push_back(std::to_string(dist(rng)));
    }
    return v;
}

static void filter_BloomFilter(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);
    for (auto _ : state) {
        BloomFilter bf(batch * 10, 3);
        for (size_t i = 0; i < batch; ++i) {
            bf.insert(keys[i]);
            benchmark::ClobberMemory();
        }
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_BloomFilter)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

static void filter_BloomFilterContains(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);
    BloomFilter bf(batch * 10, 3);
    for (auto &k : keys) bf.insert(k);

    size_t idx = 0;
    for (auto _ : state) {
        bool found = bf.contains(keys[idx]);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % batch;
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_BloomFilterContains)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

static CuckooFilter make_cf(size_t n_elements) {
    size_t buckets = std::max<size_t>(4, n_elements / 4);
    return CuckooFilter(buckets, 4, 8, 500);
}

static void filter_CuckooFilterInsert(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);

    for (auto _ : state) {
        auto cf = make_cf(batch);
        for (size_t i = 0; i < batch; ++i) {
            bool ok = cf.insert(keys[i]);
            benchmark::DoNotOptimize(ok);
        }
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooFilterInsert)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

static void filter_CuckooFilterInsertZipf(benchmark::State &state) {
    const size_t batch = state.range(0);

    // prepare a Zipfian stream of `batch` keys
    std::mt19937_64 rng(12345);
    auto zipf_dist = make_zipf(10'000'000, 1.1);
    std::vector<std::string> keys;
    keys.reserve(batch);
    for (size_t i = 0; i < batch; ++i) {
        keys.push_back(std::to_string(zipf_dist(rng)));
    }

    for (auto _ : state) {
        auto cf = make_cf(batch);
        for (size_t i = 0; i < batch; ++i) {
            bool ok = cf.insert(keys[i]);
            benchmark::ClobberMemory();
        }
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooFilterInsertZipf)
    ->Arg(1 << 12)
    ->Arg(1 << 16)
    ->Arg(1 << 20);

static void filter_CuckooFilterContains(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);
    auto cf = make_cf(batch);
    for (auto &k : keys) {
        if (!cf.insert(k)) {
            // if insertion fails (very unlikely at low load), continue
        }
    }

    size_t idx = 0;
    for (auto _ : state) {
        bool ok = cf.contains(keys[idx]);
        benchmark::DoNotOptimize(ok);
        idx = (idx + 1) % batch;
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooFilterContains)
    ->Arg(1 << 12)
    ->Arg(1 << 16)
    ->Arg(1 << 20);

static void filter_CuckooFilterErase(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);
    auto cf = make_cf(batch);
    for (auto &k : keys) cf.insert(k);

    size_t idx = 0;
    for (auto _ : state) {
        bool ok = cf.erase(keys[idx]);
        benchmark::DoNotOptimize(ok);
        cf.insert(keys[idx]);
        idx = (idx + 1) % batch;
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooFilterErase)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

static CuckooMap make_cm(size_t n_elements) {
    size_t buckets = std::max<size_t>(4, n_elements / 4);
    return CuckooMap(buckets, 4, 8, 500);
}

static void filter_CuckooMapInsert(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);
    for (auto _ : state) {
        auto cm = make_cm(batch);
        for (size_t i = 0; i < batch; ++i) {
            bool ok = cm.insert(keys[i]);
            benchmark::DoNotOptimize(ok);
        }
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooMapInsert)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

static void filter_CuckooMapInsertZipf(benchmark::State &state) {
    const size_t batch = state.range(0);

    // prepare a Zipfian stream of `batch` keys
    std::mt19937_64 rng(12345);
    auto zipf_dist = make_zipf(10'000'000, 1.1);
    std::vector<std::string> keys;
    keys.reserve(batch);
    for (size_t i = 0; i < batch; ++i) {
        keys.push_back(std::to_string(zipf_dist(rng)));
    }

    for (auto _ : state) {
        auto cm = make_cm(batch);
        for (size_t i = 0; i < batch; ++i) {
            bool ok = cm.insert(keys[i]);
            benchmark::DoNotOptimize(ok);
        }
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooMapInsertZipf)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

static void filter_CuckooMapContains(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);
    auto cm = make_cm(batch);
    for (auto &k : keys) {
        cm.insert(k);
    }

    size_t idx = 0;
    for (auto _ : state) {
        bool found = cm.contains(keys[idx]);
        benchmark::DoNotOptimize(found);
        idx = (idx + 1) % batch;
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooMapContains)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

static void filter_CuckooMapErase(benchmark::State &state) {
    const size_t batch = state.range(0);
    auto keys = make_random_strings(batch);
    auto cm = make_cm(batch);
    for (auto &k : keys) {
        cm.insert(k);
    }

    size_t idx = 0;
    for (auto _ : state) {
        bool ok = cm.erase(keys[idx]);
        benchmark::DoNotOptimize(ok);
        cm.insert(keys[idx]);
        idx = (idx + 1) % batch;
    }
    state.SetItemsProcessed(state.iterations() * batch);
}

BENCHMARK(filter_CuckooMapErase)->Arg(1 << 12)->Arg(1 << 16)->Arg(1 << 20);

BENCHMARK_MAIN();
