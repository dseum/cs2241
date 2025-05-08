#include "memtable.hpp"

#include <gtest/gtest.h>

#include <format>

using namespace mousedb::memtable;

TEST(memtable_KVStore, Insert) {
    KVStore kvs(4096);
    std::string key = "1";
    std::string value = "1";
    auto ptr = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
        std::span(reinterpret_cast<std::byte *>(value.data()), value.size()));
    EXPECT_NE(ptr, nullptr);
}

TEST(memtable_KVStore, InsertAndGetOne) {
    KVStore kvs(4096);
    std::string key = "1";
    std::string value = "1";
    auto ptr = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
        std::span(reinterpret_cast<std::byte *>(value.data()), value.size()));
    auto [retrieved_key, retrieved_value] = kvs.get(ptr);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(retrieved_key.data()),
                          retrieved_key.size()),
              key);
    EXPECT_EQ(
        std::string(reinterpret_cast<const char *>(retrieved_value.data()),
                    retrieved_value.size()),
        value);
}

TEST(memtable_KVStore, InsertAndGetMultiple) {
    KVStore kvs(4096);
    std::string key = "1";
    std::string value = "1";

    auto ptr1 = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
        std::span(reinterpret_cast<std::byte *>(value.data()), value.size()));
    auto [retrieved_key1, retrieved_value1] = kvs.get(ptr1);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(retrieved_key1.data()),
                          retrieved_key1.size()),
              key);
    EXPECT_EQ(
        std::string(reinterpret_cast<const char *>(retrieved_value1.data()),
                    retrieved_value1.size()),
        value);

    key = "2";
    value = "2";
    auto ptr2 = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
        std::span(reinterpret_cast<std::byte *>(value.data()), value.size()));
    auto [retrieved_key2, retrieved_value2] = kvs.get(ptr2);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(retrieved_key2.data()),
                          retrieved_key2.size()),
              key);
    EXPECT_EQ(
        std::string(reinterpret_cast<const char *>(retrieved_value2.data()),
                    retrieved_value2.size()),
        value);

    key = "3";
    value = "3";
    auto ptr3 = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
        std::span(reinterpret_cast<std::byte *>(value.data()), value.size()));
    auto [retrieved_key3, retrieved_value3] = kvs.get(ptr3);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(retrieved_key3.data()),
                          retrieved_key3.size()),
              key);
    EXPECT_EQ(
        std::string(reinterpret_cast<const char *>(retrieved_value3.data()),
                    retrieved_value3.size()),
        value);
}

TEST(memtable_KVStore, InsertAndGetStress) {
    KVStore kvs(4096);
    std::string key;
    std::string value;
    for (int i = 0; i < 10000; ++i) {
        key = std::to_string(i);
        value = std::to_string(i);
        auto ptr = kvs.insert(
            std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
            std::span(reinterpret_cast<std::byte *>(value.data()),
                      value.size()));
        auto [retrieved_key, retrieved_value] = kvs.get(ptr);
        EXPECT_EQ(
            std::string(reinterpret_cast<const char *>(retrieved_key.data()),
                        retrieved_key.size()),
            key);
        EXPECT_EQ(
            std::string(reinterpret_cast<const char *>(retrieved_value.data()),
                        retrieved_value.size()),
            value);
    }
}

TEST(memtable_KVStore, InsertAndCompare) {
    KVStore kvs(4096);
    std::string key1 = "1";
    std::string value1 = "1";
    auto ptr1 = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key1.data()), key1.size()),
        std::span(reinterpret_cast<std::byte *>(value1.data()), value1.size()));
    std::string key2 = "2";
    std::string value2 = "2";
    auto ptr2 = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key2.data()), key2.size()),
        std::span(reinterpret_cast<std::byte *>(value2.data()), value2.size()));
    std::string key3 = "3";
    std::string value3 = "3";
    auto ptr3 = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key3.data()), key3.size()),
        std::span(reinterpret_cast<std::byte *>(value3.data()), value3.size()));
    EXPECT_LT(KVStore::compare(ptr1, ptr2), 0);
    EXPECT_GT(KVStore::compare(ptr2, ptr1), 0);
    EXPECT_LT(KVStore::compare(ptr1, ptr3), 0);
    EXPECT_GT(KVStore::compare(ptr3, ptr1), 0);
    EXPECT_LT(KVStore::compare(ptr2, ptr3), 0);
    EXPECT_GT(KVStore::compare(ptr3, ptr2), 0);
    EXPECT_EQ(KVStore::compare(ptr1, ptr1), 0);
    EXPECT_EQ(KVStore::compare(ptr2, ptr2), 0);
    EXPECT_EQ(KVStore::compare(ptr3, ptr3), 0);
}

TEST(memtable_KVStore, ValueExceedsSliceSkip) {
    const size_t slab = 128;
    KVStore kvs(4096);

    // create a value longer than skip but less than slab/2: triggers small-heap
    // path
    std::string key = "k";
    std::string value_small(slab / 3, 'x');  // > skip but < slab/2
    auto psmall = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
        std::span(reinterpret_cast<std::byte *>(value_small.data()),
                  value_small.size()));
    auto [rk, rv] = kvs.get(psmall);
    EXPECT_EQ(rv.size(), value_small.size());
    EXPECT_EQ(std::string(reinterpret_cast<char *>(rv.data()), rv.size()),
              value_small);

    // huge value > slab/2
    std::string value_huge(slab, 'y');  // = slab > slab/2
    auto phuge = kvs.insert(
        std::span(reinterpret_cast<std::byte *>(key.data()), key.size()),
        std::span(reinterpret_cast<std::byte *>(value_huge.data()),
                  value_huge.size()));
    auto [rk2, rv2] = kvs.get(phuge);
    EXPECT_EQ(rv2.size(), value_huge.size());
    EXPECT_EQ(std::string(reinterpret_cast<char *>(rv2.data()), rv2.size()),
              value_huge);
}

TEST(memtable_KVStore, ManyThreadsInsertAndGet) {
    constexpr int THREADS = 8;
    constexpr int OPS_PER_THREAD = 1000;

    KVStore kvs(4096);

    struct Pair {
        KVStore::ptr_type p;
        std::string key;
        std::string value;
    };

    std::vector<Pair> records;
    records.reserve(THREADS * OPS_PER_THREAD);
    std::mutex rec_mutex;

    auto worker = [&](int tid) {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            std::string k = std::format("t_{}_k{}", tid, i);
            std::string v = std::format("t_{}_v{}", tid, i);
            auto p = kvs.insert(
                std::span(reinterpret_cast<std::byte *>(k.data()), k.size()),
                std::span(reinterpret_cast<std::byte *>(v.data()), v.size()));
            {
                std::scoped_lock lk(rec_mutex);
                records.push_back({p, k, v});
            }
        }
    };

    // launches threads
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) threads.emplace_back(worker, t);
    for (auto &th : threads) th.join();

    // verifies all records
    for (auto &r : records) {
        auto [rk, rv] = kvs.get(r.p);
        std::string kk(reinterpret_cast<char *>(rk.data()), rk.size());
        std::string vv(reinterpret_cast<char *>(rv.data()), rv.size());
        EXPECT_EQ(kk, r.key);
        EXPECT_EQ(vv, r.value);
    }
}

TEST(memtable_KVSkipList, InsertAndGet) {
    MemTable<KVSkipList> memtable;
    std::string key = "1";
    std::string value = "1";
    memtable.insert(key, value);
    auto retrieved_value = memtable.find(key);
    EXPECT_FALSE(retrieved_value.empty());
}

TEST(memtable_KVSkipList, InsertAndGetMultiple) {
    MemTable<KVSkipList> memtable;
    for (int i = 0; i < 1000; ++i) {
        std::string key = std::to_string(i);
        std::string value = std::to_string(i);
        memtable.insert(key, value);
    }
}
