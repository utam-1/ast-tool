#include "doc/doc_parse.hpp"

#include "pattern/date.hpp"
#include "pattern/version.hpp"
#include "util/hash.hpp"
#include "util/strings.hpp"

#include <map>

namespace docmerge::doc {

namespace {

void collectBodyTextForKey(const Block& b, std::string& out) {
    for (const auto& l : b.lead) {
        if (!out.empty()) {
            out.push_back('\n');
        }
        out.append(l.text);
    }
    for (const auto& child : b.children) {
        collectBodyTextForKey(child, out);
    }
}

std::string computeRawKey(const Block& b, const config::LevelRule& rule) {
    if (rule.keySpec == "text") {
        std::string allText;
        collectBodyTextForKey(b, allText);
        return allText;
    }
    if (util::startsWith(rule.keySpec, "capture:")) {
        const std::string capName = rule.keySpec.substr(8);
        auto it = b.captures.find(capName);
        if (it != b.captures.end() && !it->second.empty()) {
            return it->second;
        }
    }
    if (!b.lead.empty()) {
        return b.lead.front().text;
    }
    return "";
}

uint64_t computeSubtreeHash(const Block& b) {
    uint64_t h = util::kFnvOffsetBasis;
    h = util::hashCombine(h, static_cast<uint64_t>(b.level));
    for (const auto& l : b.lead) {
        h = util::hashCombine(h, l.hash);
    }
    for (const auto& c : b.children) {
        h = util::hashCombine(h, c.hash);
    }
    return h;
}

}  // namespace

void computeBlockKeysAndHashes(Block& block, const config::DocRule& docRule) {
    for (auto& child : block.children) {
        computeBlockKeysAndHashes(child, docRule);
    }

    // Assign occurrence suffixes for children with identical keys under this parent
    std::map<std::string, int> keyCounts;
    for (const auto& child : block.children) {
        if (!child.isConflict) {
            ++keyCounts[child.keyText];
        }
    }
    std::map<std::string, int> keySeen;
    for (auto& child : block.children) {
        if (child.isConflict) {
            continue;
        }
        if (keyCounts[child.keyText] > 1) {
            int idx = ++keySeen[child.keyText];
            child.occurrenceKey = child.keyText + "#" + std::to_string(idx);
        } else {
            child.occurrenceKey = child.keyText;
        }
    }

    if (block.level >= 1 && static_cast<size_t>(block.level) <= docRule.levels.size()) {
        const auto& rule = docRule.levels[static_cast<size_t>(block.level - 1)];
        const std::string rawKey = computeRawKey(block, rule);
        block.keyText = util::normalizeKey(rawKey, rule.normalize);
        if (block.occurrenceKey.empty()) {
            block.occurrenceKey = block.keyText;
        }
    }
    block.hash = computeSubtreeHash(block);
}

Document parseDocumentLines(const SplitDocumentLines& split,
                            const config::DocRule& docRule,
                            std::vector<ParseDiag>* outWarnings) {
    Document doc;
    doc.eolStyle = split.eolStyle;
    doc.hasBom = split.hasBom;
    doc.root.level = 0;
    doc.root.levelName = "root";

    std::vector<Block*> stack;
    stack.push_back(&doc.root);

    std::vector<Line> pendingBlank;

    for (size_t lineIdx = 0; lineIdx < split.lines.size(); ++lineIdx) {
        const Line& L = split.lines[lineIdx];

        int matchedLevel = 0;
        pattern::MatchResult firstMatch;
        std::vector<std::string> overlappingLevels;

        for (size_t lvlIdx = 0; lvlIdx < docRule.levels.size(); ++lvlIdx) {
            const auto& lvlRule = docRule.levels[lvlIdx];
            pattern::MatchResult mr = pattern::matchLine(lvlRule.startPattern, L.text);
            if (mr.matched) {
                if (matchedLevel == 0) {
                    matchedLevel = lvlRule.levelIndex;
                    firstMatch = std::move(mr);
                } else if (outWarnings != nullptr) {
                    overlappingLevels.push_back(lvlRule.name);
                }
            }
        }

        if (outWarnings != nullptr && !overlappingLevels.empty()) {
            const auto& winner = docRule.levels[static_cast<size_t>(matchedLevel - 1)];
            std::string msg = "line matches multiple levels ('" + winner.name + "' and '";
            for (size_t k = 0; k < overlappingLevels.size(); ++k) {
                if (k > 0) msg += "', '";
                msg += overlappingLevels[k];
            }
            msg += "'); first level '" + winner.name + "' wins";
            outWarnings->push_back(ParseDiag{static_cast<int>(lineIdx + 1), std::move(msg)});
        }

        if (matchedLevel == 0) {
            if (L.isBlank()) {
                pendingBlank.push_back(L);
            } else {
                Block* top = stack.back();
                top->lead.insert(top->lead.end(), pendingBlank.begin(), pendingBlank.end());
                pendingBlank.clear();
                top->lead.push_back(L);
            }
        } else {
            Block* outermostClosed = nullptr;
            while (stack.back()->level >= matchedLevel) {
                outermostClosed = stack.back();
                stack.pop_back();
            }

            if (outermostClosed != nullptr) {
                outermostClosed->trail.insert(outermostClosed->trail.end(),
                                              pendingBlank.begin(),
                                              pendingBlank.end());
            } else {
                Block* top = stack.back();
                top->lead.insert(top->lead.end(), pendingBlank.begin(), pendingBlank.end());
            }
            pendingBlank.clear();

            const auto& lvlRule = docRule.levels[static_cast<size_t>(matchedLevel - 1)];
            Block node;
            node.level = matchedLevel;
            node.levelName = lvlRule.name;
            node.captures = std::move(firstMatch.captures);
            node.lead.push_back(L);

            Block* parent = stack.back();
            parent->children.push_back(std::move(node));
            stack.push_back(&parent->children.back());
        }
    }

    // At EOF: outermost open real block gets pendingBlank as its trail (or root.lead if none)
    if (!pendingBlank.empty()) {
        if (stack.size() >= 2) {
            Block* outermostReal = stack[1];
            outermostReal->trail.insert(outermostReal->trail.end(),
                                        pendingBlank.begin(),
                                        pendingBlank.end());
        } else {
            doc.root.lead.insert(doc.root.lead.end(),
                                 pendingBlank.begin(),
                                 pendingBlank.end());
        }
        pendingBlank.clear();
    }

    computeBlockKeysAndHashes(doc.root, docRule);
    return doc;
}

Document parseDocument(std::string_view rawBytes,
                       const config::DocRule& docRule,
                       std::vector<ParseDiag>* outWarnings) {
    const SplitDocumentLines split = splitLines(rawBytes);
    return parseDocumentLines(split, docRule, outWarnings);
}

}  // namespace docmerge::doc
