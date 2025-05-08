#include "filter.hpp"

#include <functional>
#include <stdexcept>
#include <vector>

namespace mousedb {
namespace filter {

BloomFilter::BloomFilter(std::size_t size, std::size_t hash_count)
    : size_(size), hash_count_(hash_count), bits_(size) {
}

BloomFilter::BloomFilter(FILE *fp) {
    if (!fp) throw std::runtime_error("BloomFilter::load: null FILE*");

    std::size_t bit_count, hash_count;
    if (fread(&bit_count, sizeof(bit_count), 1, fp) != 1 ||
        fread(&hash_count, sizeof(hash_count), 1, fp) != 1) {
        throw std::runtime_error("BloomFilter::load: failed to read header");
    }

    std::size_t nblocks;
    if (fread(&nblocks, sizeof(nblocks), 1, fp) != 1) {
        throw std::runtime_error(
            "BloomFilter::load: failed to read block count");
    }

    using block_type = typename boost::dynamic_bitset<>::block_type;
    std::vector<block_type> blocks(nblocks);
    if (fread(blocks.data(), sizeof(block_type), nblocks, fp) != nblocks) {
        throw std::runtime_error("BloomFilter::load: failed to read blocks");
    }

    boost::dynamic_bitset<> bits(bit_count);
    constexpr std::size_t bits_per_block =
        boost::dynamic_bitset<>::bits_per_block;
    for (std::size_t i = 0; i < nblocks; ++i) {
        block_type blk = blocks[i];
        for (std::size_t b = 0; b < bits_per_block; ++b) {
            if (blk & (block_type(1) << b)) {
                std::size_t pos = i * bits_per_block + b;
                if (pos < bit_count) bits.set(pos);
            }
        }
    }

    size_ = bit_count;
    hash_count_ = hash_count;
    bits_ = std::move(bits);
}

auto BloomFilter::add(std::string_view item) -> void {
    for (std::size_t i = 0; i < hash_count_; ++i) {
        auto h = hash(item, i) % size_;
        bits_.set(h);
    }
}

auto BloomFilter::contains(std::string_view item) const -> bool {
    for (std::size_t i = 0; i < hash_count_; ++i) {
        auto h = hash(item, i) % size_;
        if (!bits_.test(h)) {
            return false;
        }
    }
    return true;
}

auto BloomFilter::save(FILE *fp) const -> size_t {
    if (!fp) throw std::runtime_error("BloomFilter::save: null FILE*");

    if (fwrite(&size_, sizeof(size_), 1, fp) != 1 ||
        fwrite(&hash_count_, sizeof(hash_count_), 1, fp) != 1) {
        throw std::runtime_error("BloomFilter::save: failed to write header");
    }

    std::size_t nblocks = bits_.num_blocks();
    if (fwrite(&nblocks, sizeof(nblocks), 1, fp) != 1) {
        throw std::runtime_error(
            "BloomFilter::save: failed to write block count");
    }

    using block_type = typename boost::dynamic_bitset<>::block_type;
    std::vector<block_type> blocks(nblocks);
    boost::to_block_range(bits_, blocks.begin());
    if (fwrite(blocks.data(), sizeof(block_type), nblocks, fp) != nblocks) {
        throw std::runtime_error("BloomFilter::save: failed to write blocks");
    }
    return sizeof(size_) + sizeof(hash_count_) + sizeof(nblocks) +
           nblocks * sizeof(block_type);
}

auto BloomFilter::hash(std::string_view item, std::size_t seed) const
    -> size_t {
    static std::hash<std::string_view> hasher;
    auto h1 = hasher(item);
    // 64-bit mix inspired by boost::hash_combine's magic constant
    return h1 ^ (seed + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
}

}  // namespace filter
}  // namespace mousedb
