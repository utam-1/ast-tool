#include "pattern/version.hpp"

#include <algorithm>

namespace docmerge::pattern {

namespace {

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool isPrereleaseChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           isDigit(c) || c == '.' || c == '-';
}

}  // namespace

bool matchVersionAt(std::string_view line, size_t& pos, ParsedVersion* outVer) {
    size_t cur = pos;
    if (cur < line.size() && (line[cur] == 'v' || line[cur] == 'V')) {
        ++cur;
    }

    std::vector<int> nums;
    while (cur < line.size() && isDigit(line[cur])) {
        int val = 0;
        while (cur < line.size() && isDigit(line[cur])) {
            val = val * 10 + (line[cur] - '0');
            ++cur;
        }
        nums.push_back(val);
        if (cur < line.size() && line[cur] == '.') {
            if (cur + 1 < line.size() && isDigit(line[cur + 1])) {
                ++cur;  // consume '.'
                continue;
            }
        }
        break;
    }

    // Require at least two numeric segments (e.g. "1.2" or "1.2.3")
    if (nums.size() < 2) {
        return false;
    }

    std::string prerelease;
    if (cur < line.size() && line[cur] == '-') {
        size_t preStart = cur + 1;
        size_t preEnd = preStart;
        while (preEnd < line.size() && isPrereleaseChar(line[preEnd])) {
            ++preEnd;
        }
        if (preEnd > preStart) {
            prerelease = std::string(line.substr(preStart, preEnd - preStart));
            cur = preEnd;
        }
    }

    if (outVer != nullptr) {
        outVer->numbers = std::move(nums);
        outVer->prerelease = std::move(prerelease);
    }
    pos = cur;
    return true;
}

bool parseVersion(std::string_view text, ParsedVersion& outVer) {
    size_t pos = 0;
    if (!matchVersionAt(text, pos, &outVer)) {
        return false;
    }
    return pos == text.size();
}

int compareVersions(const ParsedVersion& a, const ParsedVersion& b) {
    const size_t maxLen = std::max(a.numbers.size(), b.numbers.size());
    for (size_t i = 0; i < maxLen; ++i) {
        const int na = (i < a.numbers.size()) ? a.numbers[i] : 0;
        const int nb = (i < b.numbers.size()) ? b.numbers[i] : 0;
        if (na != nb) {
            return (na < nb) ? -1 : 1;
        }
    }

    // Pre-release sorts BEFORE final release (e.g. 2.0.0-rc1 < 2.0.0)
    if (a.prerelease.empty() && b.prerelease.empty()) {
        return 0;
    }
    if (!a.prerelease.empty() && b.prerelease.empty()) {
        return -1;
    }
    if (a.prerelease.empty() && !b.prerelease.empty()) {
        return 1;
    }
    if (a.prerelease == b.prerelease) {
        return 0;
    }
    return (a.prerelease < b.prerelease) ? -1 : 1;
}

int compareVersionStrings(std::string_view a, std::string_view b) {
    ParsedVersion va;
    ParsedVersion vb;
    const bool okA = parseVersion(a, va);
    const bool okB = parseVersion(b, vb);
    if (okA && okB) {
        return compareVersions(va, vb);
    }
    if (a == b) {
        return 0;
    }
    return (a < b) ? -1 : 1;
}

}  // namespace docmerge::pattern
