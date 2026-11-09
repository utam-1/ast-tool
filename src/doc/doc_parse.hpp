#pragma once

#include "config/config.hpp"
#include "doc/block.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace docmerge::doc {

struct ParseDiag {
    int lineNumber = 0;  // 1-indexed
    std::string message;
};

// Parses raw file bytes into a `Document` tree using `docRule`.
Document parseDocument(std::string_view rawBytes,
                       const config::DocRule& docRule,
                       std::vector<ParseDiag>* outWarnings = nullptr);

// Parses already-split lines into a `Document` tree using `docRule`.
Document parseDocumentLines(const SplitDocumentLines& split,
                            const config::DocRule& docRule,
                            std::vector<ParseDiag>* outWarnings = nullptr);

// Recomputes `keyText`, `occurrenceKey`, and `hash` for a block subtree.
void computeBlockKeysAndHashes(Block& block, const config::DocRule& docRule);

}  // namespace docmerge::doc

