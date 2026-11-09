#include "verify/invariants.hpp"

#include "doc/doc_parse.hpp"
#include "doc/line.hpp"
#include "util/strings.hpp"

#include <map>
#include <set>

namespace docmerge::verify {

namespace {

using LevelKeyPair = std::pair<int, std::string>;

void collectBlockKeys(const doc::Block& b, std::set<LevelKeyPair>& outKeys) {
    if (b.level >= 1 && !b.isConflict) {
        outKeys.insert({b.level, b.keyText});
    }
    for (const auto& c : b.children) {
        collectBlockKeys(c, outKeys);
    }
}

void collectUnchangedBlockKeys(const doc::Block& baseB,
                               const doc::Block& sideB,
                               std::set<LevelKeyPair>& outUnchanged) {
    // Map base children by occurrenceKey
    std::map<std::string, const doc::Block*> baseMap;
    for (const auto& c : baseB.children) {
        baseMap[c.occurrenceKey] = &c;
    }
    for (const auto& sc : sideB.children) {
        auto it = baseMap.find(sc.occurrenceKey);
        if (it != baseMap.end()) {
            if (it->second->hash == sc.hash) {
                outUnchanged.insert({sc.level, sc.keyText});
            }
            collectUnchangedBlockKeys(*it->second, sc, outUnchanged);
        }
    }
}

bool checkMarkerBalance(const std::vector<doc::Line>& lines,
                        int markerWidth,
                        bool expectConflicts,
                        std::string& outReason) {
    const std::string openPrefix(static_cast<size_t>(markerWidth), '<');
    const std::string basePrefix(static_cast<size_t>(markerWidth), '|');
    const std::string sepLine(static_cast<size_t>(markerWidth), '=');
    const std::string closePrefix(static_cast<size_t>(markerWidth), '>');

    bool inConflict = false;
    bool seenSep = false;
    size_t conflictRegions = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& t = lines[i].text;
        if (util::startsWith(t, openPrefix) &&
            (t.size() == openPrefix.size() || t[openPrefix.size()] == ' ')) {
            if (inConflict) {
                outReason = "nested or unbalanced '<<<<<<<' marker at line " + std::to_string(i + 1);
                return false;
            }
            inConflict = true;
            seenSep = false;
        } else if (util::startsWith(t, basePrefix) &&
                   (t.size() == basePrefix.size() || t[basePrefix.size()] == ' ')) {
            if (!inConflict || seenSep) {
                outReason = "unexpected '|||||||' marker at line " + std::to_string(i + 1);
                return false;
            }
        } else if (t == sepLine) {
            if (!inConflict || seenSep) {
                outReason = "unexpected '=======' marker at line " + std::to_string(i + 1);
                return false;
            }
            seenSep = true;
        } else if (util::startsWith(t, closePrefix) &&
                   (t.size() == closePrefix.size() || t[closePrefix.size()] == ' ')) {
            if (!inConflict || !seenSep) {
                outReason = "unbalanced '>>>>>>>' marker at line " + std::to_string(i + 1);
                return false;
            }
            inConflict = false;
            ++conflictRegions;
        }
    }

    if (inConflict) {
        outReason = "unclosed conflict marker at EOF";
        return false;
    }
    if (!expectConflicts && conflictRegions > 0) {
        outReason = "conflict markers found in supposedly clean merge output";
        return false;
    }
    return true;
}

}  // namespace

VerificationResult verifyMergedOutput(
    const doc::Document& baseDoc,
    const doc::Document& oursDoc,
    const doc::Document& theirsDoc,
    const doc::Document& mergedDoc,
    std::string_view mergedBytes,
    bool hasConflicts,
    const config::DocRule& docRule,
    const merge::ConflictMarkerConfig& markerCfg) {
    VerificationResult res;

    const doc::SplitDocumentLines splitOut = doc::splitLines(mergedBytes);

    // 4. Marker sanity check
    if (!checkMarkerBalance(splitOut.lines, markerCfg.markerWidth, hasConflicts, res.failureReason)) {
        res.ok = false;
        return res;
    }

    // When clean (no conflicts), verify all 3 structural invariants on the re-parsed tree
    if (!hasConflicts) {
        const doc::Document reparsed = doc::parseDocumentLines(splitOut, docRule, nullptr);

        std::set<LevelKeyPair> baseKeys;
        std::set<LevelKeyPair> oursKeys;
        std::set<LevelKeyPair> theirsKeys;
        std::set<LevelKeyPair> mergedKeys;
        std::set<LevelKeyPair> reparsedKeys;

        collectBlockKeys(baseDoc.root, baseKeys);
        collectBlockKeys(oursDoc.root, oursKeys);
        collectBlockKeys(theirsDoc.root, theirsKeys);
        collectBlockKeys(mergedDoc.root, mergedKeys);
        collectBlockKeys(reparsed.root, reparsedKeys);

        // 1. No lost additions
        for (const auto& ok : oursKeys) {
            if (baseKeys.count(ok) == 0 && reparsedKeys.count(ok) == 0) {
                res.ok = false;
                res.failureReason = "lost block added by ours: \"" + ok.second + "\"";
                return res;
            }
        }
        for (const auto& tk : theirsKeys) {
            if (baseKeys.count(tk) == 0 && reparsedKeys.count(tk) == 0) {
                res.ok = false;
                res.failureReason = "lost block added by theirs: \"" + tk.second + "\"";
                return res;
            }
        }

        // 2. No resurrected deletions
        std::set<LevelKeyPair> oursUnchanged;
        std::set<LevelKeyPair> theirsUnchanged;
        collectUnchangedBlockKeys(baseDoc.root, oursDoc.root, oursUnchanged);
        collectUnchangedBlockKeys(baseDoc.root, theirsDoc.root, theirsUnchanged);

        for (const auto& bk : baseKeys) {
            const bool deletedInOurs = (oursKeys.count(bk) == 0);
            const bool deletedInTheirs = (theirsKeys.count(bk) == 0);
            if (deletedInOurs && (deletedInTheirs || theirsUnchanged.count(bk) > 0)) {
                if (reparsedKeys.count(bk) > 0) {
                    res.ok = false;
                    res.failureReason = "resurrected block deleted by ours: \"" + bk.second + "\"";
                    return res;
                }
            }
            if (deletedInTheirs && (deletedInOurs || oursUnchanged.count(bk) > 0)) {
                if (reparsedKeys.count(bk) > 0) {
                    res.ok = false;
                    res.failureReason = "resurrected block deleted by theirs: \"" + bk.second + "\"";
                    return res;
                }
            }
        }

        // 3. Re-parse check: reparsed block keys must match merged tree block keys
        if (reparsedKeys != mergedKeys) {
            res.ok = false;
            res.failureReason = "re-parsed output block keys do not match merged block tree";
            return res;
        }
    }

    return res;
}

}  // namespace docmerge::verify
