#pragma once

#include <stdio.h>

#include <boost/dynamic_bitset.hpp>
#include <cstddef>
#include <string>

namespace mousedb {
namespace filter {

class BloomFilter {
   public:
    BloomFilter(std::size_t size, std::size_t hash_count);
    BloomFilter(FILE *fp);

    auto add(std::string_view item) -> void;

    auto contains(std::string_view item) const -> bool;
    auto save(FILE *fp) const -> size_t;

   private:
    std::size_t size_;
    std::size_t hash_count_;
    boost::dynamic_bitset<> bits_;

    auto hash(std::string_view item, std::size_t seed) const -> size_t;
};

}  // namespace filter
}  // namespace mousedb
