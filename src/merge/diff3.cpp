#include "merge/diff3.hpp"

#include "merge/diff.hpp"

#include <algorithm>

namespace docmerge::merge {

namespace {

bool linesExactlyEqual(const std::vector<doc::Line>& a,
                       const std::vector<doc::Line>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (!a[i].ContentEquals(b[i])) {
            return false;
        }
    }
    return true;
}

std::vector<doc::Line> applySideHunks(const std::vector<doc::Line>& base,
                                      const std::vector<doc::Line>& side,
                                      size_t clusterStart,
                                      size_t clusterEnd,
                                      const std::vector<Hunk>& hunks,
                                      size_t hFirst,
                                      size_t hLast) {
    std::vector<doc::Line> out;
    size_t curB = clusterStart;
    for (size_t idx = hFirst; idx < hLast; ++idx) {
        const Hunk& h = hunks[idx];
        for (size_t b = curB; b < h.aStart; ++b) {
            out.push_back(base[b]);
        }
        for (size_t s = h.bStart; s < h.bEnd; ++s) {
            out.push_back(side[s]);
        }
        curB = h.aEnd;
    }
    for (size_t b = curB; b < clusterEnd; ++b) {
        out.push_back(base[b]);
    }
    return out;
}

void appendCleanLines(std::vector<Diff3Chunk>& chunks,
                      const std::vector<doc::Line>& lines) {
    if (lines.empty()) {
        return;
    }
    if (!chunks.empty() && !chunks.back().isConflict) {
        chunks.back().cleanLines.insert(chunks.back().cleanLines.end(),
                                        lines.begin(), lines.end());
    } else {
        Diff3Chunk chunk;
        chunk.isConflict = false;
        chunk.cleanLines = lines;
        chunks.push_back(std::move(chunk));
    }
}

}  // namespace

std::vector<doc::Line> Diff3Result::toLines(const ConflictMarkerConfig& cfg) const {
    std::vector<doc::Line> out;
    for (const auto& chunk : chunks) {
        if (!chunk.isConflict) {
            out.insert(out.end(), chunk.cleanLines.begin(), chunk.cleanLines.end());
        } else {
            auto conflictLines = formatConflictLines(
                chunk.baseLines, chunk.oursLines, chunk.theirsLines, cfg);
            out.insert(out.end(), conflictLines.begin(), conflictLines.end());
        }
    }
    return out;
}

Diff3Result diff3Merge(const std::vector<doc::Line>& base,
                       const std::vector<doc::Line>& ours,
                       const std::vector<doc::Line>& theirs,
                       size_t maxEditDistance) {
    Diff3Result result;

    const DiffResult diffO = diffLines(base, ours, maxEditDistance);
    const DiffResult diffT = diffLines(base, theirs, maxEditDistance);

    const auto& hunksO = diffO.hunks;
    const auto& hunksT = diffT.hunks;

    size_t iO = 0;
    size_t iT = 0;
    size_t bPos = 0;

    while (iO < hunksO.size() || iT < hunksT.size()) {
        // Pick earliest hunk start in base
        size_t clusterStart = 0;
        size_t clusterEnd = 0;
        if (iO < hunksO.size() &&
            (iT >= hunksT.size() || hunksO[iO].aStart <= hunksT[iT].aStart)) {
            clusterStart = hunksO[iO].aStart;
            clusterEnd = hunksO[iO].aEnd;
        } else {
            clusterStart = hunksT[iT].aStart;
            clusterEnd = hunksT[iT].aEnd;
        }

        const size_t startO = iO;
        const size_t startT = iT;

        // Extend cluster while the next hunk on either side overlaps OR touches [clusterStart, clusterEnd]
        // Note: if both iO == startO and iT == startT initially, we must consume at least one hunk.
        bool progressed = true;
        while (progressed) {
            progressed = false;
            while (iO < hunksO.size() &&
                   (hunksO[iO].aStart < clusterEnd ||
                    (hunksO[iO].aStart == clusterEnd &&
                     (iO == startO || hunksO[iO].aStart > clusterStart || clusterStart == clusterEnd)))) {
                clusterEnd = std::max(clusterEnd, hunksO[iO].aEnd);
                ++iO;
                progressed = true;
            }
            while (iT < hunksT.size() &&
                   (hunksT[iT].aStart < clusterEnd ||
                    (hunksT[iT].aStart == clusterEnd &&
                     (iT == startT || hunksT[iT].aStart > clusterStart || clusterStart == clusterEnd)))) {
                clusterEnd = std::max(clusterEnd, hunksT[iT].aEnd);
                ++iT;
                progressed = true;
            }
        }

        // Emit unchanged base lines before clusterStart
        if (bPos < clusterStart) {
            std::vector<doc::Line> unchanged(base.begin() + static_cast<ptrdiff_t>(bPos),
                                             base.begin() + static_cast<ptrdiff_t>(clusterStart));
            appendCleanLines(result.chunks, unchanged);
        }

        const bool hasOursHunk = (iO > startO);
        const bool hasTheirsHunk = (iT > startT);

        std::vector<doc::Line> oText =
            applySideHunks(base, ours, clusterStart, clusterEnd, hunksO, startO, iO);
        std::vector<doc::Line> tText =
            applySideHunks(base, theirs, clusterStart, clusterEnd, hunksT, startT, iT);

        if (!hasOursHunk) {
            appendCleanLines(result.chunks, tText);
        } else if (!hasTheirsHunk) {
            appendCleanLines(result.chunks, oText);
        } else if (linesExactlyEqual(oText, tText)) {
            appendCleanLines(result.chunks, oText);
        } else {
            Diff3Chunk conflictChunk;
            conflictChunk.isConflict = true;
            conflictChunk.baseLines.assign(
                base.begin() + static_cast<ptrdiff_t>(clusterStart),
                base.begin() + static_cast<ptrdiff_t>(clusterEnd));
            conflictChunk.oursLines = std::move(oText);
            conflictChunk.theirsLines = std::move(tText);
            result.chunks.push_back(std::move(conflictChunk));
            result.hasConflicts = true;
            ++result.conflictCount;
        }

        bPos = clusterEnd;
    }

    if (bPos < base.size()) {
        std::vector<doc::Line> tail(base.begin() + static_cast<ptrdiff_t>(bPos), base.end());
        appendCleanLines(result.chunks, tail);
    }

    return result;
}

}  // namespace docmerge::merge

