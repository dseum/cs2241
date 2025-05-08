#pragma once
#include <iterator>
#include <optional>

namespace mousedb {
namespace hlc {
struct HLC {
    uint64_t physical_us;  // wall-clock, µs since UNIX epoch
    uint16_t logical;      // counter for same physical tick
    uint32_t node_id;      // tie-breaker

    /* partial ordering: “later” means ⟨phys,log,node⟩ lexicographically */
    friend constexpr auto operator<=>(const HLC &a, const HLC &b) {
        return std::tuple(a.physical_us, a.logical, a.node_id) <=>
               std::tuple(b.physical_us, b.logical, b.node_id);
    }
};

/* Select iterator to the element whose HLC is the greatest (LWW).
   – `It` must dereference to a type that has `.clock`  */
template <typename It>
[[nodiscard]] It lww_select(It first, It last) {
    if (first == last) return last;
    It best = first;
    for (++first; first != last; ++first)
        if (best->clock < first->clock) best = first;
    return best;
}

/* Convenience wrapper for containers that expose `begin()`/`end()` */
template <typename Container>
[[nodiscard]] auto lww_select(Container &&c)
    -> std::optional<typename Container::value_type> {
    auto it = lww_select(std::begin(c), std::end(c));
    if (it == std::end(c)) return std::nullopt;
    return *it;
}

}  // namespace hlc
}  // namespace mousedb
