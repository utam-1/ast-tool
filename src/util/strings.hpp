#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace docmerge::util {

// Removes trailing spaces and tabs (' ' and '\t').
std::string trimTrailingWhitespace(std::string_view s);

// Trims leading and trailing spaces and tabs.
std::string trimWhitespace(std::string_view s);

// Replaces runs of spaces/tabs with a single space.
std::string collapseWhitespace(std::string_view s);

// Converts ASCII uppercase letters to lowercase.
std::string toLowerAscii(std::string_view s);

// Applies a sequence of key normalizations: "trim-trailing", "collapse-ws", "ignore-case".
std::string normalizeKey(std::string_view input,
                         const std::vector<std::string>& rules);

bool startsWith(std::string_view s, std::string_view prefix) noexcept;
bool endsWith(std::string_view s, std::string_view suffix) noexcept;

}  // namespace docmerge::util

