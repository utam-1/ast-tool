#pragma once

#include <string_view>
#include <vector>
#include <string>

namespace docmerge::util {

// Matches a path against a glob pattern supporting *, **, and ?.
// Path separators are normalized to '/' before matching.
bool globMatch(std::string_view pattern, std::string_view path);

// Returns true if any pattern in `patterns` matches `path`.
bool globMatchAny(const std::vector<std::string>& patterns, std::string_view path);

}  // namespace docmerge::util

