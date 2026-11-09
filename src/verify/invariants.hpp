#pragma once

#include "config/config.hpp"
#include "doc/block.hpp"
#include "merge/conflict.hpp"

#include <string>
#include <string_view>

namespace docmerge::verify {

struct VerificationResult {
    bool ok = true;
    std::string failureReason;
};

// Verifies the 4 safety invariants from Section 7.8:
// 1. No lost additions
// 2. No resurrected deletions
// 3. Re-parse check (when clean)
// 4. Conflict marker balance and sanity
VerificationResult verifyMergedOutput(
    const doc::Document& baseDoc,
    const doc::Document& oursDoc,
    const doc::Document& theirsDoc,
    const doc::Document& mergedDoc,
    std::string_view mergedBytes,
    bool hasConflicts,
    const config::DocRule& docRule,
    const merge::ConflictMarkerConfig& markerCfg);

}  // namespace docmerge::verify
