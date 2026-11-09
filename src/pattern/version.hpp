#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace docmerge::pattern {

struct ParsedVersion {
    std::vector<int> numbers;
    std::string prerelease;  // e.g. "rc1", "alpha.1" (empty for final release)
};

// Attempts to match a dotted version (e.g. "1.2.3", "v2.0.0-rc1") at `line[pos..]`.
// Advances `pos` on success.
bool matchVersionAt(std::string_view line, size_t& pos, ParsedVersion* outVer = nullptr);

// Parses a standalone version string. Returns false if invalid.
bool parseVersion(std::string_view text, ParsedVersion& outVer);

// Compares two version strings (or ParsedVersions).
// Returns < 0 if a < b, 0 if a == b, > 0 if a > b.
// Note: A pre-release suffix such as "-rc1" sorts BEFORE the plain release ("2.0.0-rc1" < "2.0.0").
int compareVersions(const ParsedVersion& a, const ParsedVersion& b);
int compareVersionStrings(std::string_view a, std::string_view b);

}  // namespace docmerge::pattern

