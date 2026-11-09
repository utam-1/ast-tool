#include "util/hash.hpp"

namespace docmerge::util {

uint64_t fnv1a64(std::string_view data, uint64_t seed) noexcept {
    uint64_t hash = seed;
    for (unsigned char c : data) {
        hash ^= static_cast<uint64_t>(c);
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t hashCombine(uint64_t seed, uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

}  // namespace docmerge::util

