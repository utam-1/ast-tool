#pragma once

#include "doc/block.hpp"
#include "merge/conflict.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace docmerge::doc {

// Flattens a `Block` subtree into a sequence of `Line`s (emitting conflict markers for conflict nodes).
void flattenBlockLines(const Block& block,
                       const merge::ConflictMarkerConfig& markerCfg,
                       std::vector<Line>& outLines);

// Flattens an entire `Document` into `Line`s.
std::vector<Line> flattenDocumentLines(const Document& doc,
                                       const merge::ConflictMarkerConfig& markerCfg = {});

// Serializes a `Document` back into raw bytes (preserving BOM and every line's exact EOL).
std::string writeDocument(const Document& doc,
                          const merge::ConflictMarkerConfig& markerCfg = {});

// Prints the block tree in directory-tree style with byte ranges (`docmerge dump`).
void dumpDocumentTree(const Document& doc, std::ostream& out);

}  // namespace docmerge::doc
