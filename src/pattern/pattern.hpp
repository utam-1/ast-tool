#pragma once

#include "pattern/date.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace docmerge::pattern {

enum class ElementKind {
    Literal,   // "text"
    Ws,        // ws (one or more spaces or tabs)
    Tab,       // tab (single '\t')
    Spaces,    // spaces(N)
    Date,      // date(FORMAT)
    Version,   // version
    Number,    // number (one or more digits)
    Until,     // until("x")
    Any        // any (rest of the line)
};

struct PatternElement {
    ElementKind kind = ElementKind::Literal;
    std::string textArg;      // For Literal and Until
    int countArg = 0;         // For Spaces(N)
    DateFormat dateFormat;    // For Date
    std::string captureName;  // Non-empty if followed by `as NAME`
};

struct CompiledPattern {
    std::string source;
    std::vector<PatternElement> elements;
};

struct MatchResult {
    bool matched = false;
    std::map<std::string, std::string> captures;
};

// Compiles a pattern string (Section 4.3) into a `CompiledPattern`.
bool compilePattern(std::string_view src, CompiledPattern& outPattern, std::string& outError);

// Matches `pattern` from column 0 of `line`.
MatchResult matchLine(const CompiledPattern& pattern, std::string_view line);

}  // namespace docmerge::pattern
