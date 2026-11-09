#pragma once

#include "doc/line.hpp"
#include "merge/conflict.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace docmerge::merge {

struct Diff3Chunk {
    bool isConflict = false;
    std::vector<doc::Line> cleanLines;   // Valid when !isConflict
    std::vector<doc::Line> baseLines;    // Valid when isConflict
    std::vector<doc::Line> oursLines;    // Valid when isConflict
    std::vector<doc::Line> theirsLines;  // Valid when isConflict
};

struct Diff3Result {
    std::vector<Diff3Chunk> chunks;
    bool hasConflicts = false;
    size_t conflictCount = 0;

    // Flattens all chunks into a single vector of Lines, emitting markers for conflicts.
    std::vector<doc::Line> toLines(const ConflictMarkerConfig& cfg) const;
};

// Performs a 3-way line merge of `base`, `ours`, and `theirs`.
// Touching hunks are treated as overlapping (matching Git's merge behavior).
Diff3Result diff3Merge(const std::vector<doc::Line>& base,
                       const std::vector<doc::Line>& ours,
                       const std::vector<doc::Line>& theirs,
                       size_t maxEditDistance = 20000);

}  // namespace docmerge::merge

