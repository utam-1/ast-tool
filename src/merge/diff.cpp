#include "merge/diff.hpp"

#include <algorithm>

namespace docmerge::merge {

DiffResult diffLines(const std::vector<doc::Line>& a,
                     const std::vector<doc::Line>& b,
                     size_t maxEditDistance) {
    DiffResult result;

    const size_t totalA = a.size();
    const size_t totalB = b.size();

    // 1. Strip common prefix
    size_t prefix = 0;
    while (prefix < totalA && prefix < totalB &&
           a[prefix].ContentEquals(b[prefix])) {
        ++prefix;
    }

    // 2. Strip common suffix
    size_t suffix = 0;
    while (suffix < (totalA - prefix) && suffix < (totalB - prefix) &&
           a[totalA - 1 - suffix].ContentEquals(b[totalB - 1 - suffix])) {
        ++suffix;
    }

    const size_t N = totalA - prefix - suffix;
    const size_t M = totalB - prefix - suffix;

    if (N == 0 && M == 0) {
        return result;
    }
    if (N == 0) {
        result.hunks.push_back(Hunk{prefix, prefix, prefix, prefix + M});
        return result;
    }
    if (M == 0) {
        result.hunks.push_back(Hunk{prefix, prefix + N, prefix, prefix});
        return result;
    }

    const int n = static_cast<int>(N);
    const int m = static_cast<int>(M);
    const int maxD = static_cast<int>(std::min(N + M, maxEditDistance));
    const int offset = static_cast<int>(N + M);

    std::vector<int> v(static_cast<size_t>(2 * offset + 1), 0);
    std::vector<std::vector<int>> trace;
    trace.reserve(static_cast<size_t>(maxD + 1));

    int finalD = -1;
    for (int d = 0; d <= maxD; ++d) {
        trace.push_back(v);
        for (int k = -d; k <= d; k += 2) {
            int x = 0;
            if (k == -d || (k != d && v[static_cast<size_t>(k - 1 + offset)] <
                                          v[static_cast<size_t>(k + 1 + offset)])) {
                x = v[static_cast<size_t>(k + 1 + offset)];
            } else {
                x = v[static_cast<size_t>(k - 1 + offset)] + 1;
            }
            int y = x - k;
            while (x < n && y < m &&
                   a[prefix + static_cast<size_t>(x)].ContentEquals(
                       b[prefix + static_cast<size_t>(y)])) {
                ++x;
                ++y;
            }
            v[static_cast<size_t>(k + offset)] = x;
            if (x >= n && y >= m) {
                finalD = d;
                break;
            }
        }
        if (finalD != -1) {
            break;
        }
    }

    if (finalD == -1) {
        // Exceeded maxEditDistance cap: return a single replacement hunk for the middle slice
        result.tooComplex = true;
        result.hunks.push_back(Hunk{prefix, prefix + N, prefix, prefix + M});
        return result;
    }

    // Backtrack through `trace` to collect matched (x, y) pairs in [0..n) x [0..m)
    struct MatchPair {
        int x;
        int y;
    };
    std::vector<MatchPair> matches;
    int x = n;
    int y = m;
    for (int d = finalD; d > 0; --d) {
        const auto& prevV = trace[static_cast<size_t>(d)];
        const int k = x - y;
        int prevK = 0;
        if (k == -d || (k != d && prevV[static_cast<size_t>(k - 1 + offset)] <
                                      prevV[static_cast<size_t>(k + 1 + offset)])) {
            prevK = k + 1;
        } else {
            prevK = k - 1;
        }
        const int prevX = prevV[static_cast<size_t>(prevK + offset)];
        const int prevY = prevX - prevK;

        while (x > prevX && y > prevY) {
            --x;
            --y;
            matches.push_back(MatchPair{x, y});
        }
        x = prevX;
        y = prevY;
    }
    while (x > 0 && y > 0) {
        --x;
        --y;
        matches.push_back(MatchPair{x, y});
    }

    std::reverse(matches.begin(), matches.end());

    // Convert gaps between matched lines into half-open Hunks
    size_t curA = 0;
    size_t curB = 0;
    for (const auto& mp : matches) {
        const size_t matchA = static_cast<size_t>(mp.x);
        const size_t matchB = static_cast<size_t>(mp.y);
        if (curA < matchA || curB < matchB) {
            result.hunks.push_back(
                Hunk{prefix + curA, prefix + matchA, prefix + curB, prefix + matchB});
        }
        curA = matchA + 1;
        curB = matchB + 1;
    }
    if (curA < N || curB < M) {
        result.hunks.push_back(Hunk{prefix + curA, prefix + N, prefix + curB, prefix + M});
    }

    return result;
}

}  // namespace docmerge::merge

