#include "mousedb/database/core.hpp"

#include <sched.h>
#include <unistd.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <thread>

namespace fs = std::filesystem;

struct WalState {
    fs::path path;
    FILE *file;
    size_t ot;
    size_t oid;
    std::vector<char> key, value;
};

namespace mousedb {
namespace database {

thread_local size_t Database::cpu_id_ = 0;

Database::Database(const fs::path &root_path, const Options &options)
    : options_(options),
      num_cpus_(std::thread::hardware_concurrency()),
      root_path_(root_path),
      data_path_(root_path_ / "data"),
      shards_(num_cpus_),
      memtable_(std::make_shared<memtable::MemTable<memtable::KVSkipList>>()),
      sstables_(1),
      queue_(data_path_,
             std::max(std::thread::hardware_concurrency() / 2,
                      static_cast<unsigned int>(1)),
             *this) {
    if (!fs::exists(root_path_)) {
        fs::create_directory(root_path_);
        fs::create_directory(data_path_);
    }
    if (!fs::is_directory(root_path)) {
        throw std::runtime_error(
            std::format("{} is not a directory", root_path_.c_str()));
    }
    if (!fs::is_directory(data_path_)) {
        throw std::runtime_error(
            std::format("{} is not a directory", data_path_.c_str()));
    }
    if (!fs::is_empty(data_path_)) {
        // recovers WAL
        if (!options_.fresh) {
            std::vector<WalState> wals;
            for (auto &entry : fs::directory_iterator(data_path_)) {
                if (entry.path().extension() != ".wal") continue;
                std::cout << "ENTRY " << entry.path().c_str() << std::endl;
                FILE *f = fopen(entry.path().c_str(), "rb");
                if (!f) {
                    throw std::runtime_error(
                        std::format("Failed to open {}", entry.path().c_str()));
                }

                WalState ws;
                ws.file = f;
                ws.path = entry.path();
                // read one entry inline
                if (fread(&ws.ot, sizeof(ws.ot), 1, f) != 1 ||
                    fread(&ws.oid, sizeof(ws.oid), 1, f) != 1) {
                    std::cout << "HELLO " << entry.path().c_str() << std::endl;
                    fclose(f);
                    fs::remove(entry.path());
                    continue;
                }
                size_t key_sz;
                std::ignore = fread(&key_sz, sizeof(key_sz), 1, f);
                ws.key.resize(key_sz);
                std::ignore = fread(ws.key.data(), 1, key_sz, f);

                if (ws.ot == 0) {
                    size_t val_sz;
                    std::ignore = fread(&val_sz, sizeof(val_sz), 1, f);
                    ws.value.resize(val_sz);
                    std::ignore = fread(ws.value.data(), 1, val_sz, f);
                }
                wals.push_back(std::move(ws));
            }
            std::cout << "HERE" << std::endl;

            // always process the lowest-oid entry next
            while (!wals.empty()) {
                // find index of smallest oid
                size_t best = 0;
                for (size_t i = 1; i < wals.size(); ++i)
                    if (wals[i].oid < wals[best].oid) best = i;

                WalState &ws = wals[best];

                switch (ws.ot) {
                    case 0:
                        std::cout
                            << std::format("RINSERTING {}",
                                           std::string_view(ws.key.data(),
                                                            ws.key.size()))
                            << std::endl;
                        internal_insert(
                            std::string_view(ws.key.data(), ws.key.size()),
                            std::string_view(ws.value.data(), ws.value.size()));
                        break;
                    case 1:
                        internal_erase(
                            std::string_view(ws.key.data(), ws.key.size()));
                        break;
                    default:
                        throw std::runtime_error(std::format(
                            "Unknown op {} in {}", ws.ot, ws.path.c_str()));
                }

                FILE *f = ws.file;
                size_t ot, oid;
                if (fread(&ot, sizeof(ot), 1, f) != 1 ||
                    fread(&oid, sizeof(oid), 1, f) != 1) {
                    fclose(f);
                    fs::remove(ws.path);
                    wals.erase(wals.begin() + best);
                    continue;
                }

                ws.ot = ot;
                ws.oid = oid;

                size_t key_sz;
                std::ignore = fread(&key_sz, sizeof(key_sz), 1, f);
                ws.key.resize(key_sz);
                std::ignore = fread(ws.key.data(), 1, key_sz, f);

                if (ws.ot == 0) {
                    size_t val_sz;
                    std::ignore = fread(&val_sz, sizeof(val_sz), 1, f);
                    ws.value.resize(val_sz);
                    std::ignore = fread(ws.value.data(), 1, val_sz, f);
                } else {
                    ws.value.clear();
                }
            }

            // flushes memtable from wal
            if (memtable_->used() > options.flush_threshold) {
                std::cout << "FLUSHING" << std::endl;
                queue_.enqueue_memtable(std::move(memtable_));
                memtable_ = std::make_shared<
                    memtable::MemTable<memtable::KVSkipList>>();
            }
        }

        // recovers sstables and constructs bloom filter
        size_t max_used_id = 0;
        for (const auto &entry : fs::directory_iterator(data_path_)) {
            if (entry.path().extension() != ".sst") {
                continue;
            }
            std::string id_str = entry.path().filename().string();
            size_t id = std::stoull(id_str);
            if (id > max_used_id) {
                max_used_id = id;
            }
        }
        unused_sst_id_ = max_used_id + 1;
    }
    queue_.set_unused_sst_id(unused_sst_id_);
    std::cout << "CREATED" << std::endl;
    reset_shard();
    std::cout << "CREATED2" << std::endl;
    for (size_t i = 0; i < shards_.size(); ++i) {
        std::cout << "CREATED f" << i << std::endl;
        FILE *file =
            fopen((data_path_ / (std::to_string(i) + ".wal")).c_str(), "w");
        shards_[i].file = file;
    }
}

Database::~Database() {
    for (auto &shard : shards_) {
        fclose(shard.file);
    }
}

auto Database::find(std::string_view key) -> std::optional<std::string_view> {
    std::cout << "FINDING " << key << std::endl;
    return internal_find(key);
}

auto Database::insert(std::string_view key, std::string_view value,
                      hlc::HLC hclock) -> void {
    std::cout << "INSERTING " << key << std::endl;
    std::string value_copy;
    value_copy.resize(sizeof(hclock.physical_us) + sizeof(hclock.logical) +
                      sizeof(hclock.node_id));
    std::memcpy(value_copy.data(), &hclock.physical_us,
                sizeof(hclock.physical_us));
    std::memcpy(value_copy.data() + sizeof(hclock.physical_us), &hclock.logical,
                sizeof(hclock.logical));
    std::memcpy(
        value_copy.data() + sizeof(hclock.physical_us) + sizeof(hclock.logical),
        &hclock.node_id, sizeof(hclock.node_id));
    value_copy += std::string(value);
    wal_insert(key, value_copy);
    internal_insert(key, value_copy);
}

auto Database::erase(std::string_view key, hlc::HLC hclock) -> void {
    std::cout << "ERASING " << key << std::endl;
    std::string value_copy;
    value_copy.resize(sizeof(hclock.physical_us) + sizeof(hclock.logical) +
                      sizeof(hclock.node_id));
    std::memcpy(value_copy.data(), &hclock.physical_us,
                sizeof(hclock.physical_us));
    std::memcpy(value_copy.data() + sizeof(hclock.physical_us), &hclock.logical,
                sizeof(hclock.logical));
    std::memcpy(
        value_copy.data() + sizeof(hclock.physical_us) + sizeof(hclock.logical),
        &hclock.node_id, sizeof(hclock.node_id));
    wal_insert(key, value_copy);
    internal_insert(key, value_copy);
}

auto Database::internal_find(std::string_view key)
    -> std::optional<std::string_view> {
    std::vector<std::string_view> values;
    {
        std::shared_lock lock(memtable_mutex_);
        auto res = memtable_->find(key);
        values.insert(values.end(), res.begin(), res.end());
    }
    {
        queue_.find(key, values);
    }
    if (values.empty()) {
        return std::nullopt;
    }
    {
        std::scoped_lock lock(filters_mutex_, sstables_mutex_);
        for (const auto &sstable : sstables_[0]) {
            auto it = filters_.find(sstable);
            if (it == filters_.end()) {
                continue;
            }
            if (it->second->contains(std::string(key))) {
            }
        }
    }

    // convert values and get the starting HLC and put into HLC struct

    struct Item {
        std::string_view value;
        hlc::HLC clock;
    };

    std::vector<Item> hclocks;
    hclocks.reserve(values.size());
    for (const auto &value : values) {
        hlc::HLC hclock;
        std::memcpy(&hclock.physical_us, value.data(),
                    sizeof(hclock.physical_us));
        std::memcpy(&hclock.logical, value.data() + sizeof(hclock.physical_us),
                    sizeof(hclock.logical));
        std::memcpy(
            &hclock.node_id,
            value.data() + sizeof(hclock.physical_us) + sizeof(hclock.logical),
            sizeof(hclock.node_id));
        hclocks.push_back({value, hclock});
        std::println("HLC: {} {} {}", hclock.physical_us, hclock.logical,
                     hclock.node_id);
    }
    auto value = hlc::lww_select(std::move(hclocks));
    if (!value.has_value()) {
        return std::nullopt;
    }
    size_t erase_size = sizeof(Item::clock.physical_us) +
                        sizeof(Item::clock.logical) +
                        sizeof(Item::clock.node_id);
    if (value->value.size() <= erase_size) {
        return std::nullopt;
    }
    return std::string_view{
        value->value.data() + sizeof(Item::clock.physical_us) +
            sizeof(Item::clock.logical) + sizeof(Item::clock.node_id),
        value->value.size() -
            (sizeof(Item::clock.physical_us) + sizeof(Item::clock.logical) +
             sizeof(Item::clock.node_id))};
}

auto Database::internal_insert(std::string_view key, std::string_view value)
    -> void {
    memtable_->insert(key, value);
    if (memtable_->size() > options_.flush_threshold) {
        std::unique_lock lock(memtable_mutex_);
        queue_.enqueue_memtable(std::move(memtable_));
        memtable_ =
            std::make_shared<memtable::MemTable<memtable::KVSkipList>>();
    }
}

auto Database::internal_erase(std::string_view key) -> void {
    std::shared_lock lock(memtable_mutex_);
    std::ignore = key;
}

auto Database::wal_insert(std::string_view key, std::string_view value)
    -> void {
    Shard *shard = get_shard(cpu_id_);
    if (!shard->mutex.try_lock()) {
        shard = reset_shard();
        shard->mutex.lock();
    }
    std::unique_lock shard_lock(shard->mutex, std::adopt_lock);
    size_t ot = 0;
    fwrite(&ot, sizeof(size_t), 1, shard->file);
    size_t oid = operation_id_.fetch_add(1);
    fwrite(&oid, sizeof(size_t), 1, shard->file);
    size_t key_size = key.size();
    fwrite(&key_size, sizeof(size_t), 1, shard->file);
    fwrite(key.data(), sizeof(char), key_size, shard->file);
    size_t value_size = value.size();
    fwrite(&value_size, sizeof(size_t), 1, shard->file);
    fwrite(value.data(), sizeof(char), value_size, shard->file);
}

auto Database::wal_erase(std::string_view key) -> void {
    Shard *shard = get_shard(cpu_id_);
    if (!shard->mutex.try_lock()) {
        shard = reset_shard();
        shard->mutex.lock();
    }
    std::unique_lock shard_lock(shard->mutex, std::adopt_lock);
    size_t ot = 1;
    fwrite(&ot, sizeof(size_t), 1, shard->file);
    size_t oid = operation_id_.fetch_add(1);
    fwrite(&oid, sizeof(size_t), 1, shard->file);
    size_t key_size = key.size();
    fwrite(&key_size, sizeof(size_t), 1, shard->file);
    fwrite(key.data(), sizeof(char), key_size, shard->file);
}

inline auto Database::get_shard(size_t cpu_id) const -> Shard * {
    return const_cast<Shard *>(&shards_[cpu_id & (shards_.size() - 1)]);
}

inline auto Database::reset_shard() -> Shard * {
    size_t cpu_id = sched_getcpu();
    if (cpu_id == static_cast<size_t>(-1)) [[unlikely]] {
        cpu_id = std::hash<std::thread::id>()(std::this_thread::get_id());
    }
    cpu_id_ = cpu_id | shards_.size();
    return get_shard(cpu_id);
}

auto Database::compact(std::deque<size_t> &level_ssts) -> void {
    if (level_ssts.empty()) return;

    std::unique_lock read_lk(sstables_mutex_);
    auto lvl_it = std::find_if(sstables_.begin(), sstables_.end(),
                               [&](auto &dq) { return &dq == &level_ssts; });
    if (lvl_it == sstables_.end()) {
        // shouldn't happen
        return;
    }
    auto next_it = std::next(lvl_it);
    read_lk.unlock();

    std::map<std::string, std::vector<char>> merged;
    for (size_t sst_id : level_ssts) {
        fs::path p = data_path_ / std::format("{}.sst", sst_id);
        FILE *in = fopen(p.c_str(), "rb");
        if (!in)
            throw std::runtime_error("Failed to open SSTable for compaction");

        fseek(in, -static_cast<long>(sizeof(size_t)), SEEK_END);
        size_t entry_count;
        std::ignore = fread(&entry_count, sizeof(size_t), 1, in);

        long idx_pos = -static_cast<long>(sizeof(size_t) * (entry_count + 1));
        fseek(in, idx_pos, SEEK_END);
        std::vector<size_t> offsets(entry_count);
        std::ignore = fread(offsets.data(), sizeof(size_t), entry_count, in);

        // read bloom-filter size just before index[]
        fseek(in, idx_pos - static_cast<long>(sizeof(size_t)), SEEK_END);
        size_t bf_sz;
        std::ignore = fread(&bf_sz, sizeof(size_t), 1, in);

        long data_end = idx_pos - static_cast<long>(sizeof(size_t)) -
                        static_cast<long>(bf_sz);

        for (size_t i = 0; i < entry_count; ++i) {
            size_t start = offsets[i];
            size_t end = (i + 1 < entry_count ? offsets[i + 1] : data_end);
            size_t seq_sz = end - start;

            std::vector<char> buf(seq_sz);
            fseek(in, start, SEEK_SET);
            std::ignore = fread(buf.data(), 1, seq_sz, in);

            // interpret buf.data() as arena‐sequence
            auto *ptr =
                reinterpret_cast<memtable::KVStore::ptr_type>(buf.data());
            // extract key and value spans
            auto key_span = memtable::KVStore::get_key(ptr);
            auto val_span = memtable::KVStore::get_value(ptr);
            auto *raw_b = val_span.data();  // std::byte*
            auto raw_n = val_span.size();   // size_t

            merged[std::string(reinterpret_cast<const char *>(key_span.data()),
                               key_span.size())] =
                std::vector<char>(
                    reinterpret_cast<const char *>(raw_b),
                    reinterpret_cast<const char *>(raw_b) + raw_n);
        }

        fclose(in);
    }

    {
        std::scoped_lock write_lk(sstables_mutex_, filters_mutex_);
        for (size_t id : level_ssts) {
            fs::remove(data_path_ / std::format("{}.sst", id));
            filters_.erase(id);
        }
        level_ssts.clear();
    }

    auto temp_mt = std::make_shared<memtable::MemTable<memtable::KVSkipList>>();
    for (auto &[key, val_bytes] : merged) {
        temp_mt->insert(key,
                        std::string_view(val_bytes.data(), val_bytes.size()));
    }

    size_t new_id = unused_sst_id_++;
    fs::path out_path = data_path_ / std::format("{}.sst", new_id);
    FILE *out = fopen(out_path.c_str(), "wb");
    if (!out) throw std::runtime_error("Failed to create compacted SSTable");

    // build bloom & offsets as in process_memtable()
    auto bf = std::make_shared<filter::BloomFilter>(temp_mt->size() * 2, 3);
    std::vector<size_t> new_offsets;
    size_t cursor = 0;

    for (auto ptr : *temp_mt) {
        // ptr is a KVStore::ptr_type pointing to the raw sequence
        size_t seq_sz = memtable::KVStore::get_size(ptr);
        // write raw sequence bytes exactly
        auto *data_ptr = reinterpret_cast<const char *>(ptr);
        fwrite(data_ptr, 1, seq_sz, out);

        // update bloom + offsets
        auto key_span = memtable::KVStore::get_key(ptr);
        bf->add(std::string_view(
            reinterpret_cast<const char *>(key_span.data()), key_span.size()));
        new_offsets.push_back(cursor);
        cursor += seq_sz;
    }

    size_t bf_size = bf->save(out);
    fwrite(&bf_size, sizeof(size_t), 1, out);

    size_t cnt = new_offsets.size();
    fwrite(new_offsets.data(), sizeof(size_t), cnt, out);
    fwrite(&cnt, sizeof(size_t), 1, out);

    fclose(out);

    {
        std::scoped_lock write_lk(sstables_mutex_, filters_mutex_);
        if (next_it == sstables_.end()) {
            sstables_.emplace_back();
            next_it = std::prev(sstables_.end());
        }
        next_it->push_back(new_id);
        filters_[new_id] = bf;
    }
}

Database::Queue::Queue(const std::filesystem::path &data_path,
                       size_t num_workers, database::Database &db)
    : data_path_(data_path), num_workers_(num_workers), db_(db) {
    for (size_t i = 0; i < num_workers_; ++i) {
        workers_.emplace_back([&]() {
            for (;;) {
                std::shared_ptr<memtable::MemTable<memtable::KVSkipList>>
                    memtable;
                std::deque<std::shared_ptr<
                    memtable::MemTable<memtable::KVSkipList>>>::iterator it;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    queue_cv_.wait(
                        lock, [&]() { return stop_flag_ || !queue_.empty(); });
                    if (stop_flag_ && queue_.empty()) {
                        return;
                    }
                    memtable = std::move(queue_.front());
                    queue_.pop_front();
                    working_.emplace_back(memtable);
                    it = std::prev(working_.end());
                }
                process_memtable(it);
            }
        });
    }
}

Database::Queue::~Queue() {
    stop_flag_ = true;
    queue_cv_.notify_all();
    for (std::thread &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

auto Database::Queue::enqueue_memtable(
    std::shared_ptr<memtable::MemTable<memtable::KVSkipList>> memtable)
    -> void {
    {
        std::scoped_lock<std::mutex> lock(queue_mutex_);
        queue_.emplace_back(std::move(memtable));
    }
    queue_cv_.notify_one();
}

auto Database::Queue::find(std::string_view key,
                           std::vector<std::string_view> &values) -> void {
    std::unique_lock lock(queue_mutex_);
    for (const auto &memtable : std::views::reverse(queue_)) {
        auto res = memtable->find(key);
        if (!res.empty()) {
            values.insert(values.end(), res.begin(), res.end());
        }
    }
    for (const auto &memtable : std::views::reverse(working_)) {
        auto res = memtable->find(key);
        if (!res.empty()) {
            values.insert(values.end(), res.begin(), res.end());
        }
    }
}

auto Database::Queue::set_unused_sst_id(size_t id) -> void {
    unused_sst_id_ = id;
}

auto Database::Queue::process_memtable(
    std::deque<
        std::shared_ptr<memtable::MemTable<memtable::KVSkipList>>>::iterator it)
    -> void {
    auto memtable = *it;
    size_t id = unused_sst_id_++;

    std::cout << "Processing memtable with " << memtable->size() << "entries."
              << std::endl;
    FILE *fp = fopen((data_path_ / std::format("{}.sst", id)).c_str(), "w");
    std::ignore = fp;
    // data
    // bloom filter
    // size of bloom filter
    // indexes
    // size of indexes
    const auto memtable_size = memtable->size();
    std::vector<size_t> indexes(memtable_size);
    size_t index = 0;
    std::shared_ptr<filter::BloomFilter> bf =
        std::make_shared<filter::BloomFilter>(memtable_size * 2, 3);
    for (auto &seq : *memtable) {
        size_t seq_size = memtable::KVStore::get_size(seq);
        fwrite(&seq, 1, seq_size, fp);
        auto key = memtable::KVStore::get_key(seq);
        std::string_view str(reinterpret_cast<const char *>(key.data()),
                             key.size());
        bf->add(str);
        indexes.push_back(index);
        index += seq_size;
    }
    std::cout << "Writing bloom filter" << std::endl;
    {
        size_t bf_size = bf->save(fp);
        fwrite(&bf_size, sizeof(size_t), 1, fp);
        {
            std::scoped_lock lock(db_.filters_mutex_);
            db_.filters_[id] = bf;
        }
        {
            std::scoped_lock lock(db_.sstables_mutex_);
            if (db_.sstables_[0].size() >= 4) {
                db_.compact(db_.sstables_[0]);
                db_.sstables_.push_front({});
                db_.sstables_[0].push_back(id);
            } else {
                db_.sstables_[0].push_back(id);
            }
        }
    }
    std::cout << indexes.size() << " versus " << memtable_size << std::endl;
    fwrite(indexes.data(), sizeof(size_t), memtable_size, fp);
    fwrite(&memtable_size, sizeof(size_t), 1, fp);
    {
        std::scoped_lock<std::mutex> lock(queue_mutex_);
        working_.erase(it);
    }
}

}  // namespace database
}  // namespace mousedb
