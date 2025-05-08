#pragma once

#include <cstddef>
#include <cstdint>

namespace mousedb {
namespace random {
class Random {
   public:
    Random(size_t seed);

    static auto instance() -> Random *;
    auto next32() -> uint32_t;
    auto next64() -> uint64_t;

   private:
    size_t seed_;

    enum : uint32_t {
        M = 2147483647L  // 2^31-1
    };

    enum : uint64_t {
        A = 16807  // bits 14, 8, 7, 5, 2, 1, 0
    };
};

}  // namespace random
}  // namespace mousedb
