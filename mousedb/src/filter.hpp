#pragma once

#include <stdio.h>

#include <boost/dynamic_bitset.hpp>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

namespace mousedb {
namespace filter {

class BloomFilter {
   public:
    BloomFilter(size_t size, size_t hash_count);
    BloomFilter(FILE *fp);
    auto insert(std::string_view item) -> void;
    auto contains(std::string_view item) const -> bool;
    auto save(FILE *fp) const -> size_t;

   private:
    size_t size_;
    size_t hash_count_;
    boost::dynamic_bitset<> bits_;

    auto hash(std::string_view item, size_t seed) const -> size_t;
};

class CuckooFilter {
   public:
    CuckooFilter(size_t bucket_count, size_t bucket_size,
                 size_t fingerprint_size, size_t max_kicks);
    CuckooFilter(FILE *fp);
    auto insert(std::string_view item) -> bool;
    auto contains(std::string_view item) const -> bool;
    auto erase(std::string_view item) -> bool;
    auto save(FILE *fp) const -> size_t;

   private:
    size_t bucket_count_;
    size_t bucket_size_;
    size_t fingerprint_size_;
    size_t max_kicks_;

    std::unique_ptr<uint8_t[]> buckets_;
    mutable std::mt19937_64 rng_;

    auto fingerprint(std::string_view item) const -> uint8_t;
    auto index1(std::string_view item) const -> size_t;
    auto index2(size_t i1, uint8_t fp) const -> size_t;
};

class CuckooMap {
   public:
    CuckooMap(size_t bucket_count, size_t bucket_size, size_t fingerprint_size,
              size_t max_kicks);
    CuckooMap(FILE *fp);
    auto insert(std::string_view item) -> bool;
    auto contains(std::string_view item) const -> bool;
    auto erase(std::string_view item) -> bool;
    auto save(FILE *fp) const -> size_t;

   private:
    size_t bucket_count_;
    size_t bucket_size_;
    size_t fingerprint_size_;
    size_t max_kicks_;

    struct Node {
        uint8_t fingerprint;
        Node *next = nullptr;
    };

    struct Bucket {
        Node *next = nullptr;
        uint8_t fingerprints[1];
    };

    std::unique_ptr<uint8_t[]> buckets_;
    mutable std::mt19937_64 rng_;

    auto fingerprint(std::string_view item) const -> uint8_t;
    auto index1(std::string_view item) const -> size_t;
    auto index2(size_t i1, uint8_t fp) const -> size_t;
};

}  // namespace filter
}  // namespace mousedb
