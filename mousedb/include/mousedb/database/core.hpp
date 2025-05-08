#pragma once

#include <stdio.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "filter.hpp"
#include "hlce.hpp"
#include "memtable.hpp"
#include "spin_mutex.hpp"

namespace mousedb {
namespace database {

struct Options {
    bool fresh = false;
    size_t max_height = 12;
    size_t branching_factor = 4;
    size_t flush_threshold = 4096;
};

class Database {
   public:
    Database(const std::filesystem::path &root_path, const Options &options);
    ~Database();

    auto find(std::string_view key) -> std::optional<std::string_view>;
    auto insert(std::string_view key, std::string_view value,
                mousedb::hlc::HLC ts) -> void;
    auto erase(std::string_view key, mousedb::hlc::HLC ts) -> void;

   private:
    class Queue {
       public:
        Queue(const std::filesystem::path &data_path, size_t num_workers,
              Database &db);
        ~Queue();

        auto find(std::string_view key, std::vector<std::string_view> &values)
            -> void;

        auto enqueue_memtable(
            std::shared_ptr<memtable::MemTable<memtable::KVSkipList>> memtable)
            -> void;

        auto set_unused_sst_id(size_t id) -> void;

       private:
        auto process_memtable(
            std::deque<std::shared_ptr<
                memtable::MemTable<memtable::KVSkipList>>>::iterator it)
            -> void;

        const std::filesystem::path data_path_;
        const size_t num_workers_;
        Database &db_;

        std::atomic<size_t> unused_sst_id_;

        std::vector<std::thread> workers_;

        std::deque<std::shared_ptr<memtable::MemTable<memtable::KVSkipList>>>
            queue_;
        std::deque<std::shared_ptr<memtable::MemTable<memtable::KVSkipList>>>
            working_;
        std::mutex queue_mutex_;
        std::condition_variable queue_cv_;

        std::atomic<bool> stop_flag_ = false;
    };

    struct alignas(64) Shard {
        FILE *file;
        spin_mutex::SpinMutex mutex;
    };

    const Options &options_;
    const size_t num_cpus_;
    const std::filesystem::path root_path_;
    const std::filesystem::path data_path_;

    static thread_local size_t cpu_id_;
    std::vector<Shard> shards_;

    std::atomic<size_t> operation_id_ = 0;
    size_t unused_sst_id_ = 0;
    std::shared_ptr<memtable::MemTable<memtable::KVSkipList>> memtable_;
    std::shared_mutex memtable_mutex_;
    std::deque<std::deque<size_t>> sstables_;
    std::mutex sstables_mutex_;
    Queue queue_;
    std::unordered_map<size_t, std::shared_ptr<filter::BloomFilter>> filters_;
    std::shared_mutex filters_mutex_;

    auto internal_find(std::string_view key) -> std::optional<std::string_view>;
    auto internal_insert(std::string_view key, std::string_view value) -> void;
    auto internal_erase(std::string_view key) -> void;
    auto wal_insert(std::string_view key, std::string_view value) -> void;
    auto wal_erase(std::string_view key) -> void;

    auto compact(std::deque<size_t> &lvl) -> void;

    inline auto get_shard(size_t cpu_id) const -> Shard *;
    inline auto reset_shard() -> Shard *;
};

}  // namespace database
}  // namespace mousedb
