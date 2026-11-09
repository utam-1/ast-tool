#pragma once

#include "doc/line.hpp"

#include <string>
#include <vector>

namespace docmerge::merge {

enum class ConflictStyle {
    Diff3,  // Includes ||||||| base section
    Merge   // Only <<<<<<< ours and >>>>>>> theirs
};

struct ConflictMarkerConfig {
    ConflictStyle style = ConflictStyle::Diff3;
    int markerWidth = 7;
    std::string oursLabel = "ours";
    std::string baseLabel = "base";
    std::string theirsLabel = "theirs";
    std::string eol = "\n";
};

// Formats a conflict region into a sequence of `doc::Line`s with conflict markers.
std::vector<doc::Line> formatConflictLines(
    const std::vector<doc::Line>& baseLines,
    const std::vector<doc::Line>& oursLines,
    const std::vector<doc::Line>& theirsLines,
    const ConflictMarkerConfig& cfg);

}  // namespace docmerge::merge

