#pragma once

#include "doc/line.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace docmerge::doc {

struct Block {
    int level = 0;                                // 0 = root, 1..N = config levels
    std::string levelName;                        // e.g. "entry", "item"
    std::string keyText;                          // Normalized key for matching across base/ours/theirs
    std::string occurrenceKey;                    // keyText with optional "#1", "#2" if duplicate under parent
    std::map<std::string, std::string> captures;  // e.g. "date" -> "2025-03-16"
    std::vector<Line> lead;                       // Header line + continuation lines before first child
    std::vector<Block> children;
    std::vector<Line> trail;                      // Blank lines that follow this block
    uint64_t hash = 0;                            // Hash of normalized content (lead + children)

    // Conflict node state (used when a block itself is a conflict placeholder)
    bool isConflict = false;
    std::vector<Line> conflictBase;
    std::vector<Line> conflictOurs;
    std::vector<Line> conflictTheirs;
};

struct Document {
    Block root;                    // level 0; root.lead = preamble lines before first block
    std::string eolStyle = "\n";   // Majority line ending ("\n" or "\r\n")
    bool hasBom = false;
};

}  // namespace docmerge::doc
