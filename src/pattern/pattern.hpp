#pragma once

#include "pattern/date.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace docmerge::pattern {

enum class ElementKind {
    Literal,     // "text"
    Ws,          // ws (one or more spaces or tabs)
    Tab,         // tab (single '\t')
    Spaces,      // spaces(N)
    Date,        // date(FORMAT)
    Version,     // version
    Number,      // number (one or more digits)
    Until,       // until("x")
    Any,         // any (rest of the line)
    AfterBlank,  // after_blank (requires at least one preceding blank line)
    Underline    // underline("=") (requires next line to be repeated '=' of len >= 3)
};

struct PatternElement {
    ElementKind kind = ElementKind::Literal;
    std::string textArg;      // For Literal, Until, and Underline
    int countArg = 0;         // For Spaces(N)
    DateFormat dateFormat;    // For Date
    std::string captureName;  // Non-empty if followed by `as NAME`
};

struct CompiledPattern {
    std::string source;
    std::vector<PatternElement> elements;
    bool consumesNextLineUnderline = false;
};

struct MatchContext {
    bool precededByBlank = false;
    bool hasNextLine = false;
    std::string_view nextLineText;
};

struct MatchResult {
    bool matched = false;
    bool consumedNextLine = false;
    std::map<std::string, std::string> captures;
};

// Compiles a pattern string into a `CompiledPattern`.
bool compilePattern(std::string_view src, CompiledPattern& outPattern, std::string& outError);

// Matches `pattern` from column 0 of `line` with optional multi-line context.
MatchResult matchLine(const CompiledPattern& pattern,
                      std::string_view line,
                      const MatchContext& ctx = {});

}  // namespace docmerge::pattern
