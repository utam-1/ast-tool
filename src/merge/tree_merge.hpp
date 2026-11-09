#pragma once

#include "config/config.hpp"
#include "doc/block.hpp"
#include "merge/conflict.hpp"

#include <iosfwd>
#include <vector>

namespace docmerge::merge {

struct TreeMergeResult {
    doc::Document document;
    bool hasConflicts = false;
    size_t conflictCount = 0;
};

// Performs a 3-way structural tree merge of `baseDoc`, `oursDoc`, and `theirsDoc`.
TreeMergeResult mergeDocumentTrees(
    const doc::Document& baseDoc,
    const doc::Document& oursDoc,
    const doc::Document& theirsDoc,
    const config::DocRule& docRule,
    const ConflictMarkerConfig& markerCfg,
    std::ostream* explainOut = nullptr);

}  // namespace docmerge::merge

