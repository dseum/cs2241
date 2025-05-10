#include "filter.hpp"

#include <functional>
#include <stdexcept>
#include <vector>

namespace mousedb {
namespace filter {

BloomFilter::BloomFilter(size_t size, size_t hash_count)
    : size_(size), hash_count_(hash_count), bits_(size) {
}

BloomFilter::BloomFilter(FILE *fp) {
    if (!fp) throw std::runtime_error("BloomFilter::load: null FILE*");

    size_t bit_count, hash_count;
    if (fread(&bit_count, sizeof(bit_count), 1, fp) != 1 ||
        fread(&hash_count, sizeof(hash_count), 1, fp) != 1) {
        throw std::runtime_error("BloomFilter::load: failed to read header");
    }

    size_t nblocks;
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
    constexpr size_t bits_per_block = boost::dynamic_bitset<>::bits_per_block;
    for (size_t i = 0; i < nblocks; ++i) {
        block_type blk = blocks[i];
        for (size_t b = 0; b < bits_per_block; ++b) {
            if (blk & (block_type(1) << b)) {
                size_t pos = i * bits_per_block + b;
                if (pos < bit_count) bits.set(pos);
            }
        }
    }

    size_ = bit_count;
    hash_count_ = hash_count;
    bits_ = std::move(bits);
}

auto BloomFilter::insert(std::string_view item) -> void {
    for (size_t i = 0; i < hash_count_; ++i) {
        auto h = hash(item, i) % size_;
        bits_.set(h);
    }
}

auto BloomFilter::contains(std::string_view item) const -> bool {
    for (size_t i = 0; i < hash_count_; ++i) {
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

    size_t nblocks = bits_.num_blocks();
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

uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

auto BloomFilter::hash(std::string_view item, size_t i) const -> size_t {
    static std::hash<std::string_view> hasher;
    uint64_t h1 = hasher(item);
    uint64_t h2 = splitmix64(h1);
    return static_cast<size_t>((h1 + i * h2) % size_);
}

CuckooFilter::CuckooFilter(size_t bucket_count, size_t bucket_size,
                           size_t fingerprint_size, size_t max_kicks)
    : bucket_count_(bucket_count),
      bucket_size_(bucket_size),
      fingerprint_size_(fingerprint_size),
      max_kicks_(max_kicks),
      buckets_(new uint8_t[bucket_count * bucket_size]()),
      rng_(std::random_device{}()) {
    if (fingerprint_size_ == 0 || fingerprint_size_ > 8)
        throw std::invalid_argument("fingerprint_size must be 1..8");
}

CuckooFilter::CuckooFilter(FILE *fp) {
    if (!fp) throw std::runtime_error("CuckooFilter::load: null FILE*");
    if (fread(&bucket_count_, sizeof(bucket_count_), 1, fp) != 1 ||
        fread(&bucket_size_, sizeof(bucket_size_), 1, fp) != 1 ||
        fread(&fingerprint_size_, sizeof(fingerprint_size_), 1, fp) != 1 ||
        fread(&max_kicks_, sizeof(max_kicks_), 1, fp) != 1) {
        throw std::runtime_error("CuckooFilter::load: failed header read");
    }
    buckets_.reset(new uint8_t[bucket_count_ * bucket_size_]());
    for (size_t i = 0; i < bucket_count_; ++i) {
        size_t sz;
        if (fread(&sz, sizeof(sz), 1, fp) != 1)
            throw std::runtime_error("CuckooFilter::load: bucket size read");
        for (size_t j = 0; j < sz; ++j) {
            uint8_t fpv;
            if (fread(&fpv, sizeof(fpv), 1, fp) != 1)
                throw std::runtime_error(
                    "CuckooFilter::load: bucket data read");
            buckets_[i * bucket_size_ + j] = fpv;
        }
    }

    rng_.seed(std::random_device{}());
}

auto CuckooFilter::insert(std::string_view item) -> bool {
    uint8_t fp = fingerprint(item);
    size_t i1 = index1(item), i2 = index2(i1, fp);

    // try first bucket
    {
        auto base = i1 * bucket_size_;
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (buckets_[base + j] == 0) {
                buckets_[base + j] = fp;
                return true;
            }
        }
    }

    // try alternate bucket
    {
        auto base = i2 * bucket_size_;
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (buckets_[base + j] == 0) {
                buckets_[base + j] = fp;
                return true;
            }
        }
    }

    // eviction loop
    size_t idx = (rng_() & 1) ? i1 : i2;
    for (size_t kick = 0; kick < max_kicks_; ++kick) {
        auto base = idx * bucket_size_;
        std::uniform_int_distribution<size_t> dist(0, bucket_size_ - 1);
        size_t victim = dist(rng_);
        std::swap(fp, buckets_[base + victim]);
        idx = index2(idx, fp);

        base = idx * bucket_size_;
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (buckets_[base + j] == 0) {
                buckets_[base + j] = fp;
                return true;
            }
        }
    }
    return false;
}

auto CuckooFilter::contains(std::string_view item) const -> bool {
    uint8_t fp = fingerprint(item);
    size_t i1 = index1(item), i2 = index2(i1, fp);

    {
        auto base = i1 * bucket_size_;
        for (size_t j = 0; j < bucket_size_; ++j)
            if (buckets_[base + j] == fp) return true;
    }
    {
        auto base = i2 * bucket_size_;
        for (size_t j = 0; j < bucket_size_; ++j)
            if (buckets_[base + j] == fp) return true;
    }
    return false;
}

auto CuckooFilter::erase(std::string_view item) -> bool {
    uint8_t fp = fingerprint(item);
    size_t i1 = index1(item), i2 = index2(i1, fp);

    {
        auto base = i1 * bucket_size_;
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (buckets_[base + j] == fp) {
                buckets_[base + j] = 0;
                return true;
            }
        }
    }
    {
        auto base = i2 * bucket_size_;
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (buckets_[base + j] == fp) {
                buckets_[base + j] = 0;
                return true;
            }
        }
    }
    return false;
}

auto CuckooFilter::save(FILE *fp) const -> size_t {
    if (!fp) throw std::runtime_error("CuckooFilter::save: null FILE*");

    if (fwrite(&bucket_count_, sizeof(bucket_count_), 1, fp) != 1 ||
        fwrite(&bucket_size_, sizeof(bucket_size_), 1, fp) != 1 ||
        fwrite(&fingerprint_size_, sizeof(fingerprint_size_), 1, fp) != 1 ||
        fwrite(&max_kicks_, sizeof(max_kicks_), 1, fp) != 1) {
        throw std::runtime_error("CuckooFilter::save: failed header write");
    }

    size_t total = sizeof(bucket_count_) + sizeof(bucket_size_) +
                   sizeof(fingerprint_size_) + sizeof(max_kicks_);

    std::vector<uint8_t> values;
    for (size_t i = 0; i < bucket_count_; ++i) {
        auto base = i * bucket_size_;
        values.clear();
        // gather non-zero fingerprints
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (auto v = buckets_[base + j]; v != 0) values.push_back(v);
        }
        size_t sz = values.size();
        if (fwrite(&sz, sizeof(sz), 1, fp) != 1)
            throw std::runtime_error("CuckooFilter::save: bucket-size write");
        if (sz > 0) {
            if (fwrite(values.data(), sizeof(uint8_t), sz, fp) != sz)
                throw std::runtime_error(
                    "CuckooFilter::save: bucket-data write");
        }
        total += sizeof(sz) + sizeof(uint8_t) * sz;
    }
    return total;
}

auto CuckooFilter::size() const -> size_t {
    return bucket_size_ * bucket_count_ * sizeof(uint8_t);
}

auto CuckooFilter::fingerprint(std::string_view item) const -> uint8_t {
    auto h = std::hash<std::string_view>{}(item);
    uint8_t fp = static_cast<uint8_t>(h & ((1ull << fingerprint_size_) - 1));
    return fp ? fp : 1;
}

auto CuckooFilter::index1(std::string_view item) const -> size_t {
    auto h = std::hash<std::string_view>{}(item);
    return h % bucket_count_;
}

auto CuckooFilter::index2(size_t i1, uint8_t fp) const -> size_t {
    auto h = std::hash<uint8_t>{}(fp);
    return (i1 ^ (h % bucket_count_)) % bucket_count_;
}

CuckooMap::CuckooMap(size_t bucket_count, size_t bucket_size,
                     size_t fingerprint_size, size_t max_kicks)
    : bucket_count_(bucket_count),
      bucket_size_(bucket_size),
      fingerprint_size_(fingerprint_size),
      max_kicks_(max_kicks),
      buckets_(new uint8_t[bucket_count_ * (sizeof(Node *) + bucket_size_)]),
      rng_(std::random_device{}()) {
    const size_t stride = sizeof(Node *) + bucket_size_;
    std::memset(buckets_.get(), 0, bucket_count_ * stride);
}

CuckooMap::CuckooMap(FILE *fp) {
    if (!fp) throw std::runtime_error("CuckooMap::load: null FILE*");

    if (fread(&bucket_count_, sizeof(bucket_count_), 1, fp) != 1 ||
        fread(&bucket_size_, sizeof(bucket_size_), 1, fp) != 1 ||
        fread(&fingerprint_size_, sizeof(fingerprint_size_), 1, fp) != 1 ||
        fread(&max_kicks_, sizeof(max_kicks_), 1, fp) != 1) {
        throw std::runtime_error("CuckooMap::load: failed header read");
    }

    const size_t stride = sizeof(Node *) + bucket_size_;
    buckets_.reset(new uint8_t[bucket_count_ * stride]);
    std::memset(buckets_.get(), 0, bucket_count_ * stride);

    for (size_t i = 0; i < bucket_count_; ++i) {
        uint8_t *base = buckets_.get() + i * stride;

        if (fread(base + sizeof(Node *), sizeof(uint8_t), bucket_size_, fp) !=
            bucket_size_) {
            throw std::runtime_error("CuckooMap::load: bucket array read");
        }
        size_t chain_len = 0;
        if (fread(&chain_len, sizeof(chain_len), 1, fp) != 1) {
            throw std::runtime_error("CuckooMap::load: chain length read");
        }
        Node *prev = nullptr;
        for (size_t j = 0; j < chain_len; ++j) {
            uint8_t f = 0;
            if (fread(&f, sizeof(f), 1, fp) != 1) {
                throw std::runtime_error("CuckooMap::load: chain data read");
            }
            Node *n = new Node{f, nullptr};
            if (prev) {
                prev->next = n;
            } else {
                *reinterpret_cast<Node **>(base) = n;
            }
            prev = n;
        }
    }
}

auto CuckooMap::insert(std::string_view item) -> bool {
    uint8_t fp = fingerprint(item);
    size_t i1 = index1(item), i2 = index2(i1, fp);

    const size_t stride = sizeof(Node *) + bucket_size_;
    auto try_slot = [&](size_t i) {
        uint8_t *slots = buckets_.get() + i * stride + sizeof(Node *);
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (slots[j] == 0) {
                slots[j] = fp;
                return true;
            }
        }
        return false;
    };

    if (try_slot(i1) || try_slot(i2)) return true;

    size_t cur_index = (rng_() & 1) ? i1 : i2;
    uint8_t cur_fp = fp;
    for (size_t k = 0; k < max_kicks_; ++k) {
        uint8_t *slots = buckets_.get() + cur_index * stride + sizeof(Node *);
        size_t victim = rng_() % bucket_size_;
        std::swap(cur_fp, slots[victim]);
        cur_index = index2(cur_index, cur_fp);
        if (try_slot(cur_index)) return true;
    }

    uint8_t *base1 = buckets_.get() + i1 * stride;
    uint8_t *base2 = buckets_.get() + i2 * stride;

    Node *h1 = *reinterpret_cast<Node **>(base1);
    Node *h2 = *reinterpret_cast<Node **>(base2);

    while (h1 && h2) {
        h1 = h1->next;
        h2 = h2->next;
    }

    size_t target = (!h1 && h2) ? i1 : i2;

    uint8_t *baseT = buckets_.get() + target * stride;
    Node *oldHead = *reinterpret_cast<Node **>(baseT);
    Node *n = new Node{cur_fp, oldHead};
    *reinterpret_cast<Node **>(baseT) = n;
    return true;
}

auto CuckooMap::contains(std::string_view item) const -> bool {
    uint8_t fp = fingerprint(item);
    size_t i1 = index1(item), i2 = index2(i1, fp);
    const size_t stride = sizeof(Node *) + bucket_size_;

    for (size_t idx : {i1, i2}) {
        uint8_t *slots = buckets_.get() + idx * stride + sizeof(Node *);
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (slots[j] == fp) return true;
        }
        Node *h = *reinterpret_cast<Node **>(buckets_.get() + idx * stride);
        while (h) {
            if (h->fingerprint == fp) return true;
            h = h->next;
        }
    }
    return false;
}

auto CuckooMap::erase(std::string_view item) -> bool {
    uint8_t fp = fingerprint(item);
    size_t i1 = index1(item), i2 = index2(i1, fp);
    const size_t stride = sizeof(Node *) + bucket_size_;

    for (size_t idx : {i1, i2}) {
        uint8_t *slots = buckets_.get() + idx * stride + sizeof(Node *);
        for (size_t j = 0; j < bucket_size_; ++j) {
            if (slots[j] == fp) {
                slots[j] = 0;
                return true;
            }
        }
        uint8_t *base = buckets_.get() + idx * stride;
        Node *curr = *reinterpret_cast<Node **>(base);
        Node *prev = nullptr;
        while (curr) {
            if (curr->fingerprint == fp) {
                if (prev)
                    prev->next = curr->next;
                else
                    *reinterpret_cast<Node **>(base) = curr->next;
                delete curr;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    return false;
}

auto CuckooMap::save(FILE *fp) const -> size_t {
    if (!fp) throw std::runtime_error("CuckooMap::save: null FILE*");

    size_t written = 0;

    written += fwrite(&bucket_count_, sizeof(bucket_count_), 1, fp);
    written += fwrite(&bucket_size_, sizeof(bucket_size_), 1, fp);
    written += fwrite(&fingerprint_size_, sizeof(fingerprint_size_), 1, fp);
    written += fwrite(&max_kicks_, sizeof(max_kicks_), 1, fp);

    const size_t stride = sizeof(Node *) + bucket_size_;
    for (size_t i = 0; i < bucket_count_; ++i) {
        uint8_t *base = buckets_.get() + i * stride;
        written +=
            fwrite(base + sizeof(Node *), sizeof(uint8_t), bucket_size_, fp);
        Node *h = *reinterpret_cast<Node **>(base);
        size_t len = 0;
        for (Node *p = h; p; p = p->next) ++len;
        written += fwrite(&len, sizeof(len), 1, fp);
        for (Node *p = h; p; p = p->next) {
            written += fwrite(&p->fingerprint, sizeof(uint8_t), 1, fp);
        }
    }

    return written;
}

auto CuckooMap::size() const -> size_t {
    const size_t stride = sizeof(Node *) + bucket_size_;

    // bits used by the raw buckets_ array:
    //    bucket_count_ * stride bytes, each byte = 8 bits
    size_t bit_count = bucket_count_ * stride * 8;

    // bits used by all overflow nodes:
    for (size_t i = 0; i < bucket_count_; ++i) {
        // find the head pointer in this bucket
        uint8_t *base = buckets_.get() + i * stride;
        Node *cur = *reinterpret_cast<Node **>(base);

        // walk the chain, adding sizeof(Node)*8 bits per node
        while (cur) {
            bit_count += sizeof(Node) * 8;
            cur = cur->next;
        }
    }

    return bit_count;
}

auto CuckooMap::fingerprint(std::string_view item) const -> uint8_t {
    uint8_t h = static_cast<uint8_t>(std::hash<std::string_view>{}(item) &
                                     ((1u << fingerprint_size_) - 1));
    return h ? h : 1;
}

auto CuckooMap::index1(std::string_view item) const -> size_t {
    return std::hash<std::string_view>{}(item) % bucket_count_;
}

auto CuckooMap::index2(size_t i1, uint8_t fp) const -> size_t {
    return (i1 ^ (std::hash<uint8_t>{}(fp) % bucket_count_)) % bucket_count_;
}

}  // namespace filter
}  // namespace mousedb
