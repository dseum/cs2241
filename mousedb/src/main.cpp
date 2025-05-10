#include <chrono>
#include <iostream>
#include <random>
#include <string>

#include "filter.hpp"

using namespace mousedb::filter;

static BloomFilter make_bf(size_t n) {
    size_t m =
        std::ceil(-1.0 * n * std::log(0.03) / (std::pow(std::log(2), 2)));
    size_t k = std::ceil(std::log(2) * m / n);
    return BloomFilter(m, k);
}

static CuckooFilter make_cf(size_t n) {
    size_t bucket_size = 4;
    size_t buckets = std::ceil(n / (bucket_size * 0.95));
    return CuckooFilter(buckets, bucket_size, 8, 50);
}

static CuckooMap make_cm(size_t n) {
    size_t buckets = std::ceil(n / (4 * 0.95));
    return CuckooMap(buckets, 4, 8, 50);
}

std::discrete_distribution<size_t> make_zipf(size_t K, double s) {
    std::vector<double> w(K);
    for (size_t i = 0; i < K; ++i) {
        w[i] = 1.0 / std::pow(i + 1, s);
    }
    return std::discrete_distribution<size_t>(w.begin(), w.end());
}

int main(int argc, char **argv) {
    size_t N = 1'000'000;
    if (argc >= 2) {
        try {
            N = std::stoull(argv[1]);
        } catch (const std::exception &e) {
            std::cerr << "Invalid argument for N: \"" << argv[1]
                      << "\". Must be a positive integer.\n";
            return 1;
        }
    }
    std::mt19937_64 uni_rng(12345), test_rng(54321);
    std::uniform_int_distribution<uint64_t> uni_dist;
    auto zipf_dist = make_zipf(10'000'000, 1.1);

    // Uniform workload
    {
        std::cout << "=== Uniform workload ===\n";

        // BloomFilter
        auto bf = make_bf(N);
        for (size_t i = 0; i < N; ++i) {
            bf.insert(std::to_string(uni_dist(uni_rng)));
        }
        size_t bf_fp = 0;
        for (size_t i = 0; i < N; ++i) {
            if (bf.contains(std::to_string(uni_dist(test_rng)))) ++bf_fp;
        }
        std::cout << "BloomFilter false positives: " << bf_fp << " / " << N
                  << " (" << (100.0 * bf_fp / N) << "%)\n";

        // CuckooFilter
        auto cf = make_cf(N);
        size_t cf_fail = 0;
        for (size_t i = 0; i < N; ++i) {
            bool ok = cf.insert(std::to_string(uni_dist(uni_rng)));
            if (!ok) ++cf_fail;
        }
        size_t cf_fp = 0;
        for (size_t i = 0; i < N; ++i) {
            if (cf.contains(std::to_string(uni_dist(test_rng)))) ++cf_fp;
        }
        std::cout << "CuckooFilter failures: " << cf_fail
                  << ", false positives: " << cf_fp << " / " << N << " ("
                  << (100.0 * cf_fp / N) << "%)\n";

        // CuckooMap
        auto cm = make_cm(N);
        size_t cm_fail = 0;
        for (size_t i = 0; i < N; ++i) {
            bool ok = cm.insert(std::to_string(uni_dist(uni_rng)));
            if (!ok) ++cm_fail;
        }
        size_t cm_fp = 0;
        for (size_t i = 0; i < N; ++i) {
            if (cm.contains(std::to_string(uni_dist(test_rng)))) ++cm_fp;
        }
        std::cout << "CuckooMap failures:   " << cm_fail
                  << ", false positives: " << cm_fp << " / " << N << " ("
                  << (100.0 * cm_fp / N) << "%)\n";
    }

    // Zipfian workload
    {
        std::cout << "\n=== Zipfian workload ===\n";

        // BloomFilter
        auto bf = make_bf(N);
        for (size_t i = 0; i < N; ++i) {
            bf.insert(std::to_string(zipf_dist(uni_rng)));
        }
        size_t bf_fp = 0;
        for (size_t i = 0; i < N; ++i) {
            if (bf.contains(std::to_string(uni_dist(test_rng)))) ++bf_fp;
        }
        std::cout << "BloomFilter false positives: " << bf_fp << " / " << N
                  << " (" << (100.0 * bf_fp / N) << "%)\n";

        // CuckooFilter
        auto cf = make_cf(N);
        size_t cf_fail = 0;
        for (size_t i = 0; i < N; ++i) {
            bool ok = cf.insert(std::to_string(zipf_dist(uni_rng)));
            if (!ok) ++cf_fail;
        }
        size_t cf_fp = 0;
        for (size_t i = 0; i < N; ++i) {
            if (cf.contains(std::to_string(uni_dist(test_rng)))) ++cf_fp;
        }
        std::cout << "CuckooFilter failures: " << cf_fail
                  << ", false positives: " << cf_fp << " / " << N << " ("
                  << (100.0 * cf_fp / N) << "%)\n";

        // CuckooMap
        auto cm = make_cm(N);
        size_t cm_fail = 0;
        for (size_t i = 0; i < N; ++i) {
            bool ok = cm.insert(std::to_string(zipf_dist(uni_rng)));
            if (!ok) ++cm_fail;
        }
        size_t cm_fp = 0;
        for (size_t i = 0; i < N; ++i) {
            if (cm.contains(std::to_string(uni_dist(test_rng)))) ++cm_fp;
        }
        std::cout << "CuckooMap failures:   " << cm_fail
                  << ", false positives: " << cm_fp << " / " << N << " ("
                  << (100.0 * cm_fp / N) << "%)\n";
    }
    return 0;
}
