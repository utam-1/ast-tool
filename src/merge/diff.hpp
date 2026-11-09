#pragma once

#include "doc/line.hpp"

#include <cstddef>
#include <vector>

namespace docmerge::merge {

// Half-open line index ranges [aStart, aEnd) in `a` and [bStart, bEnd) in `b`.
struct Hunk {
    size_t aStart = 0;
    size_t aEnd = 0;
    size_t bStart = 0;
    size_t bEnd = 0;

    bool operator==(const Hunk& other) const noexcept {
        return aStart == other.aStart && aEnd == other.aEnd &&
               bStart == other.bStart && bEnd == other.bEnd;
    }
};

struct DiffResult {
    std::vector<Hunk> hunks;
    bool tooComplex = false;  // True if edit distance exceeded safety cap
};

constexpr size_t kDefaultMaxEditDistance = 20000;

// Computes a minimal line diff between `a` and `b` using Myers' O(ND) algorithm.
// Lines are compared by hash first, then by exact text.
DiffResult diffLines(const std::vector<doc::Line>& a,
                     const std::vector<doc::Line>& b,
                     size_t maxEditDistance = kDefaultMaxEditDistance);

}  // namespace docmerge::merge
