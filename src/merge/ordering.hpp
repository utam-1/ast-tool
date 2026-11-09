#pragma once

#include "config/config.hpp"
#include "doc/block.hpp"

#include <string>
#include <vector>

namespace docmerge::merge {

struct SurvivingChild {
    doc::Block block;
    bool inBase = false;
    bool inOurs = false;
    bool inTheirs = false;
};

struct OrderChildrenResult {
    bool reorderConflict = false;
    std::vector<doc::Block> ordered;
};

// Orders surviving children according to `levelRule` (Section 7.6) and applies
// blank-line separator normalization (Section 7.2) when new children are interleaved.
OrderChildrenResult orderAndNormalizeChildren(
    const std::vector<doc::Block>& baseChildren,
    const std::vector<doc::Block>& oursChildren,
    const std::vector<doc::Block>& theirsChildren,
    std::vector<SurvivingChild> surviving,
    const config::LevelRule& levelRule,
    const std::string& eolStyle);

}  // namespace docmerge::merge
