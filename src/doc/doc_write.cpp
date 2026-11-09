#include "doc/doc_write.hpp"

#include <ostream>

namespace docmerge::doc {

namespace {

size_t computeBlockByteSize(const Block& b) {
    size_t bytes = 0;
    for (const auto& l : b.lead) {
        bytes += l.text.size() + l.eol.size();
    }
    for (const auto& c : b.children) {
        bytes += computeBlockByteSize(c);
    }
    for (const auto& l : b.trail) {
        bytes += l.text.size() + l.eol.size();
    }
    return bytes;
}

std::string escapePreview(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    if (out.size() > 60) {
        out = out.substr(0, 57) + "...";
    }
    return out;
}

void dumpNode(const Block& node,
              std::ostream& out,
              const std::string& prefix,
              bool isRoot,
              bool isLast,
              size_t& byteCursor) {
    const size_t startByte = byteCursor;
    const size_t nodeBytes = computeBlockByteSize(node);
    const size_t endByte = startByte + nodeBytes;

    if (isRoot) {
        out << "(root) [" << startByte << ".." << endByte << ")\n";
    } else {
        out << prefix << (isLast ? "└── " : "├── ") << node.levelName << " ["
            << startByte << ".." << endByte << ")";
        if (!node.lead.empty()) {
            out << " \"" << escapePreview(node.lead.front().text) << "\"";
        }
        out << "\n";
    }

    // Advance byteCursor past `lead` before visiting children
    for (const auto& l : node.lead) {
        byteCursor += l.text.size() + l.eol.size();
    }

    const std::string childPrefix = prefix + (isRoot ? "" : (isLast ? "    " : "│   "));
    for (size_t i = 0; i < node.children.size(); ++i) {
        const bool lastChild = (i + 1 == node.children.size());
        dumpNode(node.children[i], out, childPrefix, false, lastChild, byteCursor);
    }

    // Advance byteCursor past `trail`
    for (const auto& l : node.trail) {
        byteCursor += l.text.size() + l.eol.size();
    }
}

}  // namespace

void flattenBlockLines(const Block& block,
                       const merge::ConflictMarkerConfig& markerCfg,
                       std::vector<Line>& outLines) {
    if (block.isConflict) {
        auto conflictLines = merge::formatConflictLines(
            block.conflictBase, block.conflictOurs, block.conflictTheirs, markerCfg);
        outLines.insert(outLines.end(), conflictLines.begin(), conflictLines.end());
        outLines.insert(outLines.end(), block.trail.begin(), block.trail.end());
        return;
    }

    outLines.insert(outLines.end(), block.lead.begin(), block.lead.end());
    for (const auto& child : block.children) {
        flattenBlockLines(child, markerCfg, outLines);
    }
    outLines.insert(outLines.end(), block.trail.begin(), block.trail.end());
}

std::vector<Line> flattenDocumentLines(const Document& doc,
                                       const merge::ConflictMarkerConfig& markerCfg) {
    std::vector<Line> out;
    flattenBlockLines(doc.root, markerCfg, out);
    return out;
}

std::string writeDocument(const Document& doc,
                          const merge::ConflictMarkerConfig& markerCfg) {
    const std::vector<Line> lines = flattenDocumentLines(doc, markerCfg);
    return joinLines(lines, doc.hasBom);
}

void dumpDocumentTree(const Document& doc, std::ostream& out) {
    size_t cursor = doc.hasBom ? 3 : 0;
    dumpNode(doc.root, out, "", true, true, cursor);
}

}  // namespace docmerge::doc
