#include "memtable.hpp"

#include <cstring>
#include <mutex>

size_t get_varint_size(uint64_t value) {
    size_t bits = 64 - std::countl_zero(value);
    return bits == 0 ? 1 : (bits + 6) / 7;
}

auto encode_varint(std::span<std::byte> data, uint64_t value) -> size_t {
    size_t i = 0;
    while (value > 0x7F) {
        data[i] = static_cast<std::byte>((value & 0x7F) | 0x80);
        value >>= 7;
        ++i;
    }
    data[i] = static_cast<std::byte>(value);
    return i + 1;
}

auto decode_varint(std::span<std::byte> data, uint64_t &value) -> size_t {
    size_t i = 0;
    value = 0;
    while (true) {
        value |= (static_cast<uint64_t>(data[i]) & 0x7F) << (i * 7);
        if ((static_cast<uint64_t>(data[i]) & 0x80) == 0) {
            break;
        }
        ++i;
    }
    return i + 1;
}

namespace mousedb {
namespace memtable {

KVStore::KVStore(size_t slab_size) : arena_(slab_size) {
}

auto KVStore::compare(std::span<std::byte> key_a, std::span<std::byte> key_b)
    -> int {
    size_t size_a = key_a.size();
    size_t size_b = key_b.size();
    int cmp = std::memcmp(key_a.data(), key_b.data(), std::min(size_a, size_b));
    if (cmp == 0) {
        if (size_a == size_b) {
            return 0;
        }
        return size_a < size_b ? -1 : 1;
    }
    return cmp;
}

auto KVStore::compare(ptr_type a, ptr_type b) -> int {
    uint64_t size_a, size_b;
    size_t offset_a = decode_varint({a, sizeof(size_t)}, size_a);
    size_t offset_b = decode_varint({b, sizeof(size_t)}, size_b);
    std::span<std::byte> key_a(a + offset_a, size_a);
    std::span<std::byte> key_b(b + offset_b, size_b);
    return compare(key_a, key_b);
}

auto KVStore::hash(std::span<std::byte> key) -> size_t {
    return std::hash<std::string_view>()(std::string_view(
        reinterpret_cast<const char *>(key.data()), key.size()));
}

auto KVStore::hash(ptr_type ptr) -> size_t {
    uint64_t size_key;
    size_t offset = decode_varint({ptr, sizeof(size_t)}, size_key);
    std::span<std::byte> key(ptr + offset, size_key);
    return hash(key);
}

auto KVStore::get(ptr_type ptr)
    -> std::pair<std::span<std::byte>, std::span<std::byte>> {
    size_t offset = 0;
    uint64_t size_key;
    offset += decode_varint({ptr, sizeof(size_t)}, size_key);
    std::span<std::byte> key(ptr + offset, size_key);
    offset += size_key;
    uint64_t size_value;
    offset += decode_varint({ptr + offset, sizeof(size_t)}, size_value);
    std::span<std::byte> value(ptr + offset, size_value);
    return {key, value};
}

auto KVStore::get_key(ptr_type ptr) -> std::span<std::byte> {
    size_t offset = 0;
    uint64_t size_key;
    offset += decode_varint({ptr, sizeof(size_t)}, size_key);
    return {ptr + offset, size_key};
}

auto KVStore::get_value(ptr_type ptr) -> std::span<std::byte> {
    size_t offset = 0;
    uint64_t size_key;
    offset += decode_varint({ptr, sizeof(size_t)}, size_key);
    offset += size_key;
    uint64_t size_value;
    offset += decode_varint({ptr + offset, sizeof(size_t)}, size_value);
    return {ptr + offset, size_value};
}

auto KVStore::get_size(ptr_type ptr) -> size_t {
    size_t offset = 0;
    uint64_t size_key;
    offset += decode_varint({ptr, sizeof(size_t)}, size_key);
    std::span<std::byte> key(ptr + offset, size_key);
    offset += size_key;
    uint64_t size_value;
    offset += decode_varint({ptr + offset, sizeof(size_t)}, size_value);
    return offset + size_value;
}

auto KVStore::insert(std::span<std::byte> key, std::span<std::byte> value)
    -> ptr_type {
    const auto size_key = key.size();
    const auto size_value = value.size();
    const auto size_varint_key = get_varint_size(size_key);
    const auto size_varint_value = get_varint_size(size_value);
    const auto size =
        size_varint_key + size_varint_value + size_key + size_value;

    // Copies into ptr as [key_size, key, value_size, value]
    auto ptr = arena_.allocate(size);
    encode_varint({ptr, size_varint_key}, size_key);
    std::memcpy(ptr + size_varint_key, key.data(), size_key);
    encode_varint({ptr + size_varint_key + size_key, size_varint_value},
                  size_value);
    std::memcpy(ptr + size_varint_key + size_key + size_varint_value,
                value.data(), size_value);
    return ptr;
}

auto KVStore::used() const -> size_t {
    return arena_.used();
}

auto KVStore::size() const -> size_t {
    return arena_.size();
}

KVSkipList::KVSkipList(size_t max_height, size_t branching_factor)
    : max_height_(max_height), branching_factor_(branching_factor), kvs_(4096) {
    std::ignore = max_height_;
    std::ignore = branching_factor_;
}

auto KVSkipList::find(std::span<std::byte> key) const
    -> std::vector<std::span<std::byte>> {
    std::vector<std::span<std::byte>> values;
    std::shared_lock lock(nodes_mutex_);
    for (const auto &node : nodes_) {
        auto [test_key, value] = KVStore::get(node);
        if (KVStore::compare(key, test_key) == 0) {
            values.push_back(value);
        }
    }
    return values;
}

auto KVSkipList::insert(std::span<std::byte> key, std::span<std::byte> value)
    -> void {
    std::unique_lock lock(nodes_mutex_);
    auto ptr = kvs_.insert(key, value);
    nodes_.push_back(ptr);
}

}  // namespace memtable
}  // namespace mousedb
