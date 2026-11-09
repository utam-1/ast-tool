#pragma once

#include <cstdint>
#include <string_view>

namespace docmerge::util {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

// Computes 64-bit FNV-1a hash of a byte view.
uint64_t fnv1a64(std::string_view data, uint64_t seed = kFnvOffsetBasis) noexcept;

// Combines an existing hash with another 64-bit value.
uint64_t hashCombine(uint64_t seed, uint64_t value) noexcept;

}  // namespace docmerge::util

