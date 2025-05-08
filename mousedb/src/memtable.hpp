#pragma once

#include <condition_variable>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>

#include "arena.hpp"

namespace mousedb {
namespace memtable {
// Stores KV pairs, allowing for concurrent inserts.
// Example:
//    KVStore kvs(1024, 256, 64);
//    auto ptr = kvs.insert("key", "value");
//    auto [key, value] = kvs.convert(ptr);
class KVStore {
   public:
    using ptr_type = arena::ConcurrentArena::ptr_type;

    explicit KVStore(size_t slab_size);

    static auto compare(std::span<std::byte> key_a, std::span<std::byte> key_b)
        -> int;
    static auto compare(ptr_type a, ptr_type b) -> int;
    static auto hash(std::span<std::byte> key) -> size_t;
    static auto hash(ptr_type ptr) -> size_t;

    static auto get(ptr_type ptr)
        -> std::pair<std::span<std::byte>, std::span<std::byte>>;
    static auto get_key(ptr_type ptr) -> std::span<std::byte>;
    static auto get_value(ptr_type ptr) -> std::span<std::byte>;
    static auto get_size(ptr_type ptr) -> size_t;

    auto insert(std::span<std::byte> key, std::span<std::byte> value)
        -> ptr_type;
    auto used() const -> size_t;
    auto size() const -> size_t;

   private:
    arena::ConcurrentArena arena_;
};

class KVSkipList {
   public:
    explicit KVSkipList(size_t max_height = 12, size_t branching_factor = 4);
    KVSkipList(const KVSkipList &) = delete;
    KVSkipList &operator=(const KVSkipList &) = delete;

    auto find(std::span<std::byte> key) const
        -> std::vector<std::span<std::byte>>;
    auto insert(std::span<std::byte> key, std::span<std::byte> value) -> void;
    auto erase(std::span<std::byte> key) -> void;

    auto used() const -> size_t {
        return kvs_.used();
    }

    auto size() const -> size_t {
        return nodes_.size();
    }

    auto begin() {
        return nodes_.begin();
    }

    auto end() {
        return nodes_.end();
    }

   private:
    struct Splice {};

    const size_t max_height_;
    const size_t branching_factor_;

    KVStore kvs_;
    std::deque<KVStore::ptr_type> nodes_;
    mutable std::shared_mutex nodes_mutex_;
};

template <typename T>
concept KVMap = requires(const T &t, T &u, std::span<std::byte> key,
                         std::span<std::byte> value) {
    { t.find(key) } -> std::same_as<std::vector<std::span<std::byte>>>;
    { u.insert(key, value) } -> std::same_as<void>;
    { u.used() } -> std::same_as<size_t>;
    { u.size() } -> std::same_as<size_t>;
};

template <KVMap T>
class MemTable {
   public:
    auto find(std::string_view key) const -> std::vector<std::string_view>;
    auto insert(std::string_view key, std::string_view value) -> void;

    auto used() const -> size_t;
    auto size() const -> size_t;

    auto begin() {
        return byte_map_.begin();
    }

    auto end() {
        return byte_map_.end();
    }

   private:
    T byte_map_;
};

template <KVMap T>
auto MemTable<T>::find(std::string_view key) const
    -> std::vector<std::string_view> {
    std::vector<std::string_view> values;
    auto results =
        byte_map_.find({const_cast<std::byte *>(
                            reinterpret_cast<const std::byte *>(key.data())),
                        key.size()});
    for (const auto &result : results) {
        values.emplace_back(reinterpret_cast<const char *>(result.data()),
                            result.size());
    }
    return values;
}

template <KVMap T>
auto MemTable<T>::insert(std::string_view key, std::string_view value) -> void {
    byte_map_.insert({const_cast<std::byte *>(
                          reinterpret_cast<const std::byte *>(key.data())),
                      key.size()},
                     {const_cast<std::byte *>(
                          reinterpret_cast<const std::byte *>(value.data())),
                      value.size()});
}

template <KVMap T>
auto MemTable<T>::used() const -> size_t {
    return byte_map_.used();
}

template <KVMap T>
auto MemTable<T>::size() const -> size_t {
    return byte_map_.size();
}

}  // namespace memtable
}  // namespace mousedb
