#include "merge/ordering.hpp"

#include "pattern/date.hpp"
#include "pattern/version.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace docmerge::merge {

namespace {

std::vector<std::string> extractKeySubsequence(
    const std::vector<doc::Block>& list,
    const std::set<std::string>& allowedKeys) {
    std::vector<std::string> out;
    for (const auto& b : list) {
        if (allowedKeys.count(b.occurrenceKey) > 0) {
            out.push_back(b.occurrenceKey);
        }
    }
    return out;
}

std::string getSortCaptureText(const doc::Block& b, const config::LevelRule& rule) {
    if (!rule.sortBy.empty()) {
        auto it = b.captures.find(rule.sortBy);
        if (it != b.captures.end()) {
            return it->second;
        }
    }
    return b.keyText;
}

bool hasValidSortKey(const doc::Block& b, const config::LevelRule& rule) {
    if (rule.order == config::OrderMode::None) {
        return false;
    }
    if (rule.order == config::OrderMode::AlphaAsc ||
        rule.order == config::OrderMode::AlphaDesc) {
        return !b.keyText.empty();
    }
    const std::string cap = getSortCaptureText(b, rule);
    if (cap.empty()) {
        return false;
    }
    if (rule.order == config::OrderMode::DateAsc ||
        rule.order == config::OrderMode::DateDesc) {
        return pattern::parseDateStringToInt(cap) >= 0;
    }
    if (rule.order == config::OrderMode::VersionAsc ||
        rule.order == config::OrderMode::VersionDesc) {
        pattern::ParsedVersion pv;
        return pattern::parseVersion(cap, pv);
    }
    return false;
}

// Returns < 0 if `a` should appear before `b` under `rule.order`, 0 if equal sort key, > 0 if `a` after `b`.
int compareBySortOrder(const doc::Block& a,
                       const doc::Block& b,
                       const config::LevelRule& rule) {
    switch (rule.order) {
        case config::OrderMode::DateAsc:
        case config::OrderMode::DateDesc: {
            const int da = pattern::parseDateStringToInt(getSortCaptureText(a, rule));
            const int db = pattern::parseDateStringToInt(getSortCaptureText(b, rule));
            if (da == db) return 0;
            const int cmp = (da < db) ? -1 : 1;
            return (rule.order == config::OrderMode::DateAsc) ? cmp : -cmp;
        }
        case config::OrderMode::VersionAsc:
        case config::OrderMode::VersionDesc: {
            const int cmp = pattern::compareVersionStrings(
                getSortCaptureText(a, rule), getSortCaptureText(b, rule));
            if (cmp == 0) return 0;
            return (rule.order == config::OrderMode::VersionAsc) ? cmp : -cmp;
        }
        case config::OrderMode::AlphaAsc:
        case config::OrderMode::AlphaDesc: {
            const std::string ka = getSortCaptureText(a, rule);
            const std::string kb = getSortCaptureText(b, rule);
            if (ka == kb) return 0;
            const int cmp = (ka < kb) ? -1 : 1;
            return (rule.order == config::OrderMode::AlphaAsc) ? cmp : -cmp;
        }
        case config::OrderMode::None:
            return 0;
    }
    return 0;
}

// Compute modal trailing blank count among non-last siblings across reference lists.
size_t computeModalGap(const std::vector<doc::Block>& baseChildren,
                       const std::vector<doc::Block>& oursChildren,
                       const std::vector<doc::Block>& theirsChildren) {
    std::map<size_t, size_t> freq;
    auto tallyList = [&](const std::vector<doc::Block>& list) {
        if (list.size() < 2) return;
        for (size_t i = 0; i + 1 < list.size(); ++i) {
            ++freq[list[i].trail.size()];
        }
    };
    tallyList(baseChildren);
    tallyList(oursChildren);
    tallyList(theirsChildren);

    if (freq.empty()) {
        // Fallback: if any single block has a non-empty trail in ours/theirs, check if it's 1
        for (const auto& b : oursChildren) {
            if (!b.trail.empty()) return b.trail.size();
        }
        for (const auto& b : theirsChildren) {
            if (!b.trail.empty()) return b.trail.size();
        }
        return 0;
    }

    size_t bestGap = 0;
    size_t bestCount = 0;
    for (const auto& kv : freq) {
        if (kv.second > bestCount) {
            bestCount = kv.second;
            bestGap = kv.first;
        }
    }
    return bestGap;
}

std::vector<doc::Line> makeBlankLines(size_t count, const std::string& eolStyle) {
    std::vector<doc::Line> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(doc::makeLine("", eolStyle));
    }
    return out;
}

}  // namespace

OrderChildrenResult orderAndNormalizeChildren(
    const std::vector<doc::Block>& baseChildren,
    const std::vector<doc::Block>& oursChildren,
    const std::vector<doc::Block>& theirsChildren,
    std::vector<SurvivingChild> surviving,
    const config::LevelRule& levelRule,
    const std::string& eolStyle) {
    OrderChildrenResult result;
    if (surviving.empty()) {
        return result;
    }

    std::map<std::string, SurvivingChild> byKey;
    std::set<std::string> baseSurvivingKeys;
    for (auto& sc : surviving) {
        if (sc.inBase) {
            baseSurvivingKeys.insert(sc.block.occurrenceKey);
        }
        byKey[sc.block.occurrenceKey] = std::move(sc);
    }

    // 1. Build skeleton of surviving base children and check for conflicting reorderings
    const std::vector<std::string> baseOrder =
        extractKeySubsequence(baseChildren, baseSurvivingKeys);
    const std::vector<std::string> oursOrder =
        extractKeySubsequence(oursChildren, baseSurvivingKeys);
    const std::vector<std::string> theirsOrder =
        extractKeySubsequence(theirsChildren, baseSurvivingKeys);

    std::vector<std::string> skeletonKeys = oursOrder;
    if (oursOrder != theirsOrder) {
        if (oursOrder == baseOrder) {
            skeletonKeys = theirsOrder;  // theirs reordered, ours didn't
        } else if (theirsOrder == baseOrder) {
            skeletonKeys = oursOrder;    // ours reordered, theirs didn't
        } else {
            // Both sides reordered surviving children differently
            result.reorderConflict = true;
            return result;
        }
    }

    std::vector<SurvivingChild> orderedList;
    orderedList.reserve(byKey.size());
    for (const auto& k : skeletonKeys) {
        orderedList.push_back(byKey.at(k));
    }

    // Collect new children (!inBase)
    std::vector<SurvivingChild> newChildren;
    for (const auto& kv : byKey) {
        if (!kv.second.inBase) {
            newChildren.push_back(kv.second);
        }
    }

    if (newChildren.empty()) {
        for (auto& sc : orderedList) {
            result.ordered.push_back(std::move(sc.block));
        }
        return result;
    }

    // Helper to find a new child's position-based anchor in `orderedList`
    auto insertPositionBased = [&](const std::vector<SurvivingChild>& toPlace) {
        // Group new children by their anchor relative to skeletonKeys:
        // Anchor is either ("after", prevSkeletonKey), ("before", nextSkeletonKey), or ("default", "")
        struct SlotKey {
            int kind;  // 0 = before key, 1 = after key, 2 = default (top/bottom)
            std::string anchorKey;
            bool operator<(const SlotKey& o) const noexcept {
                if (kind != o.kind) return kind < o.kind;
                return anchorKey < o.anchorKey;
            }
        };

        auto findSlotInSide = [&](const std::string& childKey,
                                  const std::vector<doc::Block>& sideList) -> SlotKey {
            int pos = -1;
            for (size_t i = 0; i < sideList.size(); ++i) {
                if (sideList[i].occurrenceKey == childKey) {
                    pos = static_cast<int>(i);
                    break;
                }
            }
            if (pos >= 0) {
                for (int i = pos - 1; i >= 0; --i) {
                    const std::string& k = sideList[static_cast<size_t>(i)].occurrenceKey;
                    if (baseSurvivingKeys.count(k) > 0) {
                        return SlotKey{1, k};  // after preceding surviving base sibling
                    }
                }
                for (size_t i = static_cast<size_t>(pos + 1); i < sideList.size(); ++i) {
                    const std::string& k = sideList[i].occurrenceKey;
                    if (baseSurvivingKeys.count(k) > 0) {
                        return SlotKey{0, k};  // before following surviving base sibling
                    }
                }
            }
            return SlotKey{2, ""};
        };

        std::map<SlotKey, std::vector<SurvivingChild>> oursBySlot;
        std::map<SlotKey, std::vector<SurvivingChild>> theirsBySlot;

        // Preserve each side's own relative order within a slot
        std::set<std::string> toPlaceSet;
        for (const auto& sc : toPlace) {
            toPlaceSet.insert(sc.block.occurrenceKey);
        }

        std::set<std::string> placedInOursSlot;
        for (const auto& b : oursChildren) {
            if (toPlaceSet.count(b.occurrenceKey) > 0) {
                const auto& sc = byKey.at(b.occurrenceKey);
                SlotKey sk = findSlotInSide(b.occurrenceKey, oursChildren);
                oursBySlot[sk].push_back(sc);
                placedInOursSlot.insert(b.occurrenceKey);
            }
        }
        for (const auto& b : theirsChildren) {
            if (toPlaceSet.count(b.occurrenceKey) > 0 &&
                placedInOursSlot.count(b.occurrenceKey) == 0) {
                const auto& sc = byKey.at(b.occurrenceKey);
                SlotKey sk = findSlotInSide(b.occurrenceKey, theirsChildren);
                theirsBySlot[sk].push_back(sc);
            }
        }

        std::set<SlotKey> allSlots;
        for (const auto& kv : oursBySlot) allSlots.insert(kv.first);
        for (const auto& kv : theirsBySlot) allSlots.insert(kv.first);

        for (const auto& sk : allSlots) {
            auto& gO = oursBySlot[sk];
            auto& gT = theirsBySlot[sk];
            std::vector<SurvivingChild> combined;
            if (gO.empty()) {
                combined = std::move(gT);
            } else if (gT.empty()) {
                combined = std::move(gO);
            } else {
                if (levelRule.tieBreak == config::TieBreakMode::OursFirst) {
                    combined.insert(combined.end(), gO.begin(), gO.end());
                    combined.insert(combined.end(), gT.begin(), gT.end());
                } else if (levelRule.tieBreak == config::TieBreakMode::TheirsFirst) {
                    combined.insert(combined.end(), gT.begin(), gT.end());
                    combined.insert(combined.end(), gO.begin(), gO.end());
                } else {
                    // Lexical tie-break: symmetric across ours and theirs
                    combined.insert(combined.end(), gO.begin(), gO.end());
                    combined.insert(combined.end(), gT.begin(), gT.end());
                    std::sort(combined.begin(), combined.end(),
                              [](const SurvivingChild& a, const SurvivingChild& b) {
                                  if (a.block.keyText != b.block.keyText) {
                                      return a.block.keyText < b.block.keyText;
                                  }
                                  return a.block.hash < b.block.hash;
                              });
                }
            }

            if (sk.kind == 1) {
                // Insert right after anchorKey
                auto it = std::find_if(orderedList.begin(), orderedList.end(),
                                       [&](const SurvivingChild& x) {
                                           return x.block.occurrenceKey == sk.anchorKey;
                                       });
                if (it != orderedList.end()) {
                    orderedList.insert(it + 1, combined.begin(), combined.end());
                } else {
                    orderedList.insert(orderedList.end(), combined.begin(), combined.end());
                }
            } else if (sk.kind == 0) {
                // Insert right before anchorKey
                auto it = std::find_if(orderedList.begin(), orderedList.end(),
                                       [&](const SurvivingChild& x) {
                                           return x.block.occurrenceKey == sk.anchorKey;
                                       });
                orderedList.insert(it, combined.begin(), combined.end());
            } else {
                // Default insert position (top or bottom)
                if (levelRule.insert == config::InsertPosition::Top) {
                    orderedList.insert(orderedList.begin(), combined.begin(), combined.end());
                } else {
                    orderedList.insert(orderedList.end(), combined.begin(), combined.end());
                }
            }
        }
    };

    if (levelRule.order == config::OrderMode::None) {
        insertPositionBased(newChildren);
    } else {
        std::vector<SurvivingChild> sortableNew;
        std::vector<SurvivingChild> unsortableNew;
        for (const auto& sc : newChildren) {
            if (hasValidSortKey(sc.block, levelRule)) {
                sortableNew.push_back(sc);
            } else {
                unsortableNew.push_back(sc);
            }
        }

        // Sort `sortableNew` deterministically by (sortOrder, lexical keyText, hash)
        std::sort(sortableNew.begin(), sortableNew.end(),
                  [&](const SurvivingChild& a, const SurvivingChild& b) {
                      const int cmp = compareBySortOrder(a.block, b.block, levelRule);
                      if (cmp != 0) return cmp < 0;
                      if (a.block.keyText != b.block.keyText) {
                          return a.block.keyText < b.block.keyText;
                      }
                      return a.block.hash < b.block.hash;
                  });

        // Insert each sorted new child before the first existing child in `orderedList`
        // that should come strictly AFTER it (equal sort keys go after existing equal ones).
        for (const auto& nc : sortableNew) {
            auto insertIt = orderedList.end();
            for (auto it = orderedList.begin(); it != orderedList.end(); ++it) {
                if (hasValidSortKey(it->block, levelRule)) {
                    const int cmp = compareBySortOrder(nc.block, it->block, levelRule);
                    if (cmp < 0) {
                        insertIt = it;
                        break;
                    }
                }
            }
            orderedList.insert(insertIt, nc);
        }

        if (!unsortableNew.empty()) {
            insertPositionBased(unsortableNew);
        }
    }

    // Apply blank-line separator normalization (Section 7.2) when new children are added
    const size_t gap = computeModalGap(baseChildren, oursChildren, theirsChildren);
    if (orderedList.size() >= 2) {
        const std::string prevBaseLastKey =
            baseChildren.empty() ? "" : baseChildren.back().occurrenceKey;

        // If a new block is now at the very end and has no trail, inherit terminal trail
        // from the previously-last base sibling if appropriate.
        if (!orderedList.back().inBase && orderedList.back().block.trail.empty() &&
            !prevBaseLastKey.empty()) {
            for (size_t i = 0; i + 1 < orderedList.size(); ++i) {
                if (orderedList[i].block.occurrenceKey == prevBaseLastKey &&
                    !orderedList[i].block.trail.empty()) {
                    orderedList.back().block.trail = orderedList[i].block.trail;
                    break;
                }
            }
        }

        for (size_t i = 0; i + 1 < orderedList.size(); ++i) {
            const bool isNew = !orderedList[i].inBase;
            const bool wasBaseLast =
                (!prevBaseLastKey.empty() &&
                 orderedList[i].block.occurrenceKey == prevBaseLastKey);
            if ((isNew || wasBaseLast) && orderedList[i].block.trail.size() != gap) {
                orderedList[i].block.trail = makeBlankLines(gap, eolStyle);
            }
        }
    }

    for (auto& sc : orderedList) {
        result.ordered.push_back(std::move(sc.block));
    }
    return result;
}

}  // namespace docmerge::merge
