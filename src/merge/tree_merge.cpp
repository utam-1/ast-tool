#include "merge/tree_merge.hpp"

#include "doc/doc_parse.hpp"
#include "doc/doc_write.hpp"
#include "merge/diff3.hpp"
#include "merge/ordering.hpp"

#include <map>
#include <ostream>
#include <set>

namespace docmerge::merge {

namespace {

// Aligns `occurrenceKey`s across `base`, `ours`, and `theirs` children so that
// duplicate `keyText` blocks (such as multiple commits by the same author on the same day
// in GCC ChangeLogs) match unchanged base blocks by content hash first before positional fallback.
void alignOccurrenceKeys(std::vector<doc::Block>& baseChildren,
                         std::vector<doc::Block>& oursChildren,
                         std::vector<doc::Block>& theirsChildren) {
    std::map<std::string, std::vector<size_t>> bByKey;
    std::map<std::string, std::vector<size_t>> oByKey;
    std::map<std::string, std::vector<size_t>> tByKey;
    std::set<std::string> allKeyTexts;

    for (size_t i = 0; i < baseChildren.size(); ++i) {
        bByKey[baseChildren[i].keyText].push_back(i);
        allKeyTexts.insert(baseChildren[i].keyText);
    }
    for (size_t i = 0; i < oursChildren.size(); ++i) {
        oByKey[oursChildren[i].keyText].push_back(i);
        allKeyTexts.insert(oursChildren[i].keyText);
    }
    for (size_t i = 0; i < theirsChildren.size(); ++i) {
        tByKey[theirsChildren[i].keyText].push_back(i);
        allKeyTexts.insert(theirsChildren[i].keyText);
    }

    for (const std::string& K : allKeyTexts) {
        const auto& bIdxs = bByKey[K];
        const auto& oIdxs = oByKey[K];
        const auto& tIdxs = tByKey[K];

        if (bIdxs.size() <= 1 && oIdxs.size() <= 1 && tIdxs.size() <= 1) {
            for (size_t idx : bIdxs) baseChildren[idx].occurrenceKey = K;
            for (size_t idx : oIdxs) oursChildren[idx].occurrenceKey = K;
            for (size_t idx : tIdxs) theirsChildren[idx].occurrenceKey = K;
            continue;
        }

        // Assign canonical keys K#1 .. K#p to base occurrences
        for (size_t i = 0; i < bIdxs.size(); ++i) {
            baseChildren[bIdxs[i]].occurrenceKey = K + "#" + std::to_string(i + 1);
        }

        auto matchSideToBase = [&](std::vector<doc::Block>& sideChildren,
                                   const std::vector<size_t>& sIdxs) {
            std::vector<bool> bUsed(bIdxs.size(), false);
            std::vector<bool> sUsed(sIdxs.size(), false);

            // Pass 1: Exact hash match with base
            for (size_t sj = 0; sj < sIdxs.size(); ++sj) {
                const uint64_t sHash = sideChildren[sIdxs[sj]].hash;
                for (size_t bi = 0; bi < bIdxs.size(); ++bi) {
                    if (!bUsed[bi] && baseChildren[bIdxs[bi]].hash == sHash) {
                        sideChildren[sIdxs[sj]].occurrenceKey =
                            baseChildren[bIdxs[bi]].occurrenceKey;
                        bUsed[bi] = true;
                        sUsed[sj] = true;
                        break;
                    }
                }
            }

            // Pass 2: Match remaining modified blocks in order with remaining base blocks
            for (size_t sj = 0; sj < sIdxs.size(); ++sj) {
                if (sUsed[sj]) continue;
                for (size_t bi = 0; bi < bIdxs.size(); ++bi) {
                    if (!bUsed[bi]) {
                        sideChildren[sIdxs[sj]].occurrenceKey =
                            baseChildren[bIdxs[bi]].occurrenceKey;
                        bUsed[bi] = true;
                        sUsed[sj] = true;
                        break;
                    }
                }
            }

            // Return indices into sideChildren that were newly added beyond base count
            std::vector<size_t> extraSideIdxs;
            for (size_t sj = 0; sj < sIdxs.size(); ++sj) {
                if (!sUsed[sj]) {
                    extraSideIdxs.push_back(sIdxs[sj]);
                }
            }
            return extraSideIdxs;
        };

        std::vector<size_t> extraO = matchSideToBase(oursChildren, oIdxs);
        std::vector<size_t> extraT = matchSideToBase(theirsChildren, tIdxs);

        // Pass 3: Align newly added blocks with key K between ours and theirs
        std::vector<bool> extraTUsed(extraT.size(), false);
        size_t sharedAddCounter = 0;

        for (size_t oj = 0; oj < extraO.size(); ++oj) {
            const uint64_t oHash = oursChildren[extraO[oj]].hash;
            bool matchedIdentical = false;
            for (size_t tj = 0; tj < extraT.size(); ++tj) {
                if (!extraTUsed[tj] && theirsChildren[extraT[tj]].hash == oHash) {
                    ++sharedAddCounter;
                    const std::string sharedKey =
                        K + "#add_shared_" + std::to_string(sharedAddCounter);
                    oursChildren[extraO[oj]].occurrenceKey = sharedKey;
                    theirsChildren[extraT[tj]].occurrenceKey = sharedKey;
                    extraTUsed[tj] = true;
                    matchedIdentical = true;
                    break;
                }
            }
            if (!matchedIdentical) {
                // If base had 0 occurrences and both sides added 1 occurrence (e.g. new release header),
                // let them share the key so `same_key = "merge"` can merge their children.
                if (bIdxs.empty() && extraO.size() == 1 && extraT.size() == 1 && !extraTUsed[0]) {
                    const std::string sharedKey = K + "#add_shared_1";
                    oursChildren[extraO[oj]].occurrenceKey = sharedKey;
                    theirsChildren[extraT[0]].occurrenceKey = sharedKey;
                    extraTUsed[0] = true;
                } else {
                    oursChildren[extraO[oj]].occurrenceKey =
                        K + "#add_ours_" + std::to_string(oj + 1);
                }
            }
        }
        for (size_t tj = 0; tj < extraT.size(); ++tj) {
            if (!extraTUsed[tj]) {
                theirsChildren[extraT[tj]].occurrenceKey =
                    K + "#add_theirs_" + std::to_string(tj + 1);
            }
        }
    }
}

const doc::Block* findChildByOccurrenceKey(const std::vector<doc::Block>& children,
                                           const std::string& occKey) {
    for (const auto& c : children) {
        if (c.occurrenceKey == occKey) {
            return &c;
        }
    }
    return nullptr;
}

doc::Block makeConflictNode(int level,
                            const std::string& levelName,
                            const std::string& keyText,
                            const std::string& occKey,
                            const doc::Block* b,
                            const doc::Block* o,
                            const doc::Block* t,
                            const ConflictMarkerConfig& markerCfg) {
    doc::Block node;
    node.level = level;
    node.levelName = levelName;
    node.keyText = keyText;
    node.occurrenceKey = occKey;
    node.isConflict = true;

    if (b != nullptr) {
        doc::flattenBlockLines(*b, markerCfg, node.conflictBase);
    }
    if (o != nullptr) {
        doc::flattenBlockLines(*o, markerCfg, node.conflictOurs);
    }
    if (t != nullptr) {
        doc::flattenBlockLines(*t, markerCfg, node.conflictTheirs);
    }
    return node;
}

doc::Block mergeBlockRecursive(const doc::Block* b,
                               const doc::Block& o,
                               const doc::Block& t,
                               const config::DocRule& docRule,
                               const ConflictMarkerConfig& markerCfg,
                               const std::string& eolStyle,
                               std::ostream* explainOut,
                               size_t& conflictCount);

bool mergeChildrenList(std::vector<doc::Block> baseChildren,
                       std::vector<doc::Block> oursChildren,
                       std::vector<doc::Block> theirsChildren,
                       int childLevel,
                       const config::DocRule& docRule,
                       const ConflictMarkerConfig& markerCfg,
                       const std::string& eolStyle,
                       std::ostream* explainOut,
                       std::vector<doc::Block>& outMergedChildren,
                       size_t& conflictCount) {
    if (baseChildren.empty() && oursChildren.empty() && theirsChildren.empty()) {
        return true;
    }

    alignOccurrenceKeys(baseChildren, oursChildren, theirsChildren);

    // Collect union of all occurrenceKeys preserving appearance order
    std::vector<std::string> allKeys;
    std::set<std::string> seenKeys;
    auto addKeysFrom = [&](const std::vector<doc::Block>& list) {
        for (const auto& c : list) {
            if (seenKeys.insert(c.occurrenceKey).second) {
                allKeys.push_back(c.occurrenceKey);
            }
        }
    };
    addKeysFrom(baseChildren);
    addKeysFrom(oursChildren);
    addKeysFrom(theirsChildren);

    std::vector<SurvivingChild> surviving;

    for (const std::string& occKey : allKeys) {
        const doc::Block* b = findChildByOccurrenceKey(baseChildren, occKey);
        const doc::Block* o = findChildByOccurrenceKey(oursChildren, occKey);
        const doc::Block* t = findChildByOccurrenceKey(theirsChildren, occKey);

        const int actualLvl = (o != nullptr)   ? o->level
                              : (t != nullptr) ? t->level
                              : (b != nullptr) ? b->level
                                               : childLevel;
        const size_t ruleIdx =
            (actualLvl >= 1 && static_cast<size_t>(actualLvl) <= docRule.levels.size())
                ? static_cast<size_t>(actualLvl - 1)
                : 0;
        const config::LevelRule& lvlRule = docRule.levels[ruleIdx];

        if (b == nullptr) {
            // Not in base (new block)
            if (o != nullptr && t == nullptr) {
                if (explainOut != nullptr) {
                    *explainOut << "[explain] keep ours new " << lvlRule.name
                                << " key=\"" << o->keyText << "\"\n";
                }
                surviving.push_back(SurvivingChild{*o, false, true, false});
            } else if (o == nullptr && t != nullptr) {
                if (explainOut != nullptr) {
                    *explainOut << "[explain] keep theirs new " << lvlRule.name
                                << " key=\"" << t->keyText << "\"\n";
                }
                surviving.push_back(SurvivingChild{*t, false, false, true});
            } else if (o != nullptr && t != nullptr) {
                if (o->hash == t->hash) {
                    if (explainOut != nullptr) {
                        *explainOut << "[explain] keep identical dual-added " << lvlRule.name
                                    << " key=\"" << o->keyText << "\"\n";
                    }
                    surviving.push_back(SurvivingChild{*o, false, true, true});
                } else if (lvlRule.sameKey == config::SameKeyPolicy::Merge &&
                           static_cast<size_t>(actualLvl) < docRule.levels.size()) {
                    if (explainOut != nullptr) {
                        *explainOut << "[explain] merge dual-added " << lvlRule.name
                                    << " key=\"" << o->keyText << "\"\n";
                    }
                    doc::Block mergedChild = mergeBlockRecursive(
                        nullptr, *o, *t, docRule, markerCfg, eolStyle, explainOut, conflictCount);
                    surviving.push_back(SurvivingChild{std::move(mergedChild), false, true, true});
                } else {
                    if (explainOut != nullptr) {
                        *explainOut << "[explain] conflict dual-added " << lvlRule.name
                                    << " key=\"" << o->keyText << "\"\n";
                    }
                    doc::Block confNode = makeConflictNode(
                        actualLvl, lvlRule.name, o->keyText, occKey, nullptr, o, t, markerCfg);
                    ++conflictCount;
                    surviving.push_back(SurvivingChild{std::move(confNode), false, true, true});
                }
            }
        } else {
            // Existed in base
            if (o == nullptr && t == nullptr) {
                if (explainOut != nullptr) {
                    *explainOut << "[explain] delete both-removed " << lvlRule.name
                                << " key=\"" << b->keyText << "\"\n";
                }
                continue;
            }
            if (o == nullptr && t != nullptr) {
                if (t->hash == b->hash) {
                    if (explainOut != nullptr) {
                        *explainOut << "[explain] delete ours-removed " << lvlRule.name
                                    << " key=\"" << b->keyText << "\"\n";
                    }
                    continue;
                }
                if (explainOut != nullptr) {
                    *explainOut << "[explain] conflict delete/modify " << lvlRule.name
                                << " key=\"" << b->keyText << "\"\n";
                }
                doc::Block confNode = makeConflictNode(
                    actualLvl, lvlRule.name, b->keyText, occKey, b, nullptr, t, markerCfg);
                ++conflictCount;
                surviving.push_back(SurvivingChild{std::move(confNode), true, false, true});
                continue;
            }
            if (o != nullptr && t == nullptr) {
                if (o->hash == b->hash) {
                    if (explainOut != nullptr) {
                        *explainOut << "[explain] delete theirs-removed " << lvlRule.name
                                    << " key=\"" << b->keyText << "\"\n";
                    }
                    continue;
                }
                if (explainOut != nullptr) {
                    *explainOut << "[explain] conflict modify/delete " << lvlRule.name
                                << " key=\"" << b->keyText << "\"\n";
                }
                doc::Block confNode = makeConflictNode(
                    actualLvl, lvlRule.name, b->keyText, occKey, b, o, nullptr, markerCfg);
                ++conflictCount;
                surviving.push_back(SurvivingChild{std::move(confNode), true, true, false});
                continue;
            }

            // Present in base, ours, and theirs
            const bool oChanged = (o->hash != b->hash);
            const bool tChanged = (t->hash != b->hash);
            if (!oChanged && !tChanged) {
                surviving.push_back(SurvivingChild{*o, true, true, true});
            } else if (oChanged && !tChanged) {
                if (explainOut != nullptr) {
                    *explainOut << "[explain] take ours modified " << lvlRule.name
                                << " key=\"" << o->keyText << "\"\n";
                }
                surviving.push_back(SurvivingChild{*o, true, true, true});
            } else if (!oChanged && tChanged) {
                if (explainOut != nullptr) {
                    *explainOut << "[explain] take theirs modified " << lvlRule.name
                                << " key=\"" << t->keyText << "\"\n";
                }
                surviving.push_back(SurvivingChild{*t, true, true, true});
            } else {
                if (o->hash == t->hash) {
                    surviving.push_back(SurvivingChild{*o, true, true, true});
                } else {
                    if (explainOut != nullptr) {
                        *explainOut << "[explain] recurse into modified " << lvlRule.name
                                    << " key=\"" << o->keyText << "\"\n";
                    }
                    doc::Block mergedChild = mergeBlockRecursive(
                        b, *o, *t, docRule, markerCfg, eolStyle, explainOut, conflictCount);
                    surviving.push_back(SurvivingChild{std::move(mergedChild), true, true, true});
                }
            }
        }
    }

    if (surviving.empty()) {
        return true;
    }

    const int domLvl = surviving.front().block.level;
    const size_t domIdx =
        (domLvl >= 1 && static_cast<size_t>(domLvl) <= docRule.levels.size())
            ? static_cast<size_t>(domLvl - 1)
            : 0;
    const config::LevelRule& orderRule = docRule.levels[domIdx];

    OrderChildrenResult ordRes = orderAndNormalizeChildren(
        baseChildren, oursChildren, theirsChildren, std::move(surviving), orderRule, eolStyle);
    if (ordRes.reorderConflict) {
        return false;
    }
    outMergedChildren = std::move(ordRes.ordered);
    return true;
}

doc::Block mergeBlockRecursive(const doc::Block* b,
                               const doc::Block& o,
                               const doc::Block& t,
                               const config::DocRule& docRule,
                               const ConflictMarkerConfig& markerCfg,
                               const std::string& eolStyle,
                               std::ostream* explainOut,
                               size_t& conflictCount) {
    doc::Block out;
    out.level = o.level;
    out.levelName = o.levelName;
    out.keyText = o.keyText;
    out.occurrenceKey = o.occurrenceKey;
    out.captures = o.captures;

    const std::vector<doc::Line> emptyLines;
    const std::vector<doc::Line>& bLead =
        (b != nullptr) ? b->lead : (o.lead == t.lead ? o.lead : emptyLines);
    const Diff3Result leadDiff3 = diff3Merge(bLead, o.lead, t.lead);
    if (leadDiff3.hasConflicts) {
        conflictCount += leadDiff3.conflictCount;
    }
    out.lead = leadDiff3.toLines(markerCfg);

    const std::vector<doc::Block> emptyChildren;
    const std::vector<doc::Block>& bChildren = (b != nullptr) ? b->children : emptyChildren;
    std::vector<doc::Block> mergedChildren;
    const bool orderOk = mergeChildrenList(bChildren,
                                           o.children,
                                           t.children,
                                           o.level + 1,
                                           docRule,
                                           markerCfg,
                                           eolStyle,
                                           explainOut,
                                           mergedChildren,
                                           conflictCount);
    if (!orderOk) {
        if (explainOut != nullptr) {
            *explainOut << "[explain] conflict: children reordered differently on both sides under "
                        << o.levelName << " \"" << o.keyText << "\"\n";
        }
        ++conflictCount;
        return makeConflictNode(
            o.level, o.levelName, o.keyText, o.occurrenceKey, b, &o, &t, markerCfg);
    }
    out.children = std::move(mergedChildren);

    if (b != nullptr) {
        if (o.trail.size() == b->trail.size()) {
            out.trail = t.trail;
        } else {
            out.trail = o.trail;
        }
    } else {
        out.trail = !o.trail.empty() ? o.trail : t.trail;
    }

    return out;
}

}  // namespace

TreeMergeResult mergeDocumentTrees(
    const doc::Document& baseDoc,
    const doc::Document& oursDoc,
    const doc::Document& theirsDoc,
    const config::DocRule& docRule,
    const ConflictMarkerConfig& markerCfg,
    std::ostream* explainOut) {
    TreeMergeResult res;
    res.document.eolStyle = oursDoc.eolStyle;
    res.document.hasBom = oursDoc.hasBom || baseDoc.hasBom;

    if (oursDoc.root.hash == baseDoc.root.hash &&
        oursDoc.root.children.size() == baseDoc.root.children.size()) {
        const std::string bBytes = doc::writeDocument(baseDoc, markerCfg);
        const std::string oBytes = doc::writeDocument(oursDoc, markerCfg);
        if (bBytes == oBytes) {
            if (explainOut != nullptr) {
                *explainOut << "[explain] ours unchanged from base; taking theirs\n";
            }
            res.document = theirsDoc;
            return res;
        }
    }
    if (theirsDoc.root.hash == baseDoc.root.hash &&
        theirsDoc.root.children.size() == baseDoc.root.children.size()) {
        const std::string bBytes = doc::writeDocument(baseDoc, markerCfg);
        const std::string tBytes = doc::writeDocument(theirsDoc, markerCfg);
        if (bBytes == tBytes) {
            if (explainOut != nullptr) {
                *explainOut << "[explain] theirs unchanged from base; taking ours\n";
            }
            res.document = oursDoc;
            return res;
        }
    }
    if (oursDoc.root.hash == theirsDoc.root.hash) {
        const std::string oBytes = doc::writeDocument(oursDoc, markerCfg);
        const std::string tBytes = doc::writeDocument(theirsDoc, markerCfg);
        if (oBytes == tBytes) {
            if (explainOut != nullptr) {
                *explainOut << "[explain] ours and theirs are identical\n";
            }
            res.document = oursDoc;
            return res;
        }
    }

    size_t conflictCount = 0;
    res.document.root = mergeBlockRecursive(&baseDoc.root,
                                            oursDoc.root,
                                            theirsDoc.root,
                                            docRule,
                                            markerCfg,
                                            oursDoc.eolStyle,
                                            explainOut,
                                            conflictCount);
    doc::computeBlockKeysAndHashes(res.document.root, docRule);
    res.conflictCount = conflictCount;
    res.hasConflicts = (conflictCount > 0);
    return res;
}

}  // namespace docmerge::merge
