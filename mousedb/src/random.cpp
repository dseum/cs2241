#include "random.hpp"

#include <thread>

namespace mousedb {
namespace random {

Random::Random(size_t seed) : seed_(seed) {
}

auto Random::instance() -> Random * {
    static thread_local Random *instance;
    Random *rv = instance;
    if (rv == nullptr) [[unlikely]] {
        size_t seed = std::hash<std::thread::id>{}(std::this_thread::get_id());
        rv = new Random(seed);
        instance = rv;
    }
    return rv;
}

inline auto Random::next32() -> uint32_t {
    uint64_t product = seed_ * A;
    seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
    if (seed_ > M) {
        seed_ -= M;
    }
    return seed_;
}

inline auto Random::next64() -> uint64_t {
    return (uint64_t{next32()} << 32) | next32();
}
}  // namespace random
}  // namespace mousedb
