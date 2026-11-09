#include "pattern/pattern.hpp"

#include "pattern/version.hpp"
#include "util/strings.hpp"

namespace docmerge::pattern {

namespace {

bool isSpaceOrTab(char c) noexcept {
    return c == ' ' || c == '\t';
}

bool isIdentChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

void skipSpaces(std::string_view s, size_t& i) noexcept {
    while (i < s.size() && isSpaceOrTab(s[i])) {
        ++i;
    }
}

bool parseQuotedLiteral(std::string_view s, size_t& i, std::string& outText, std::string& outErr) {
    if (i >= s.size() || s[i] != '"') {
        outErr = "expected '\"' to start string literal in pattern";
        return false;
    }
    ++i;  // skip opening '"'
    outText.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (i >= s.size()) {
                outErr = "unterminated escape in pattern string literal";
                return false;
            }
            char esc = s[i++];
            if (esc == 'n') outText.push_back('\n');
            else if (esc == 't') outText.push_back('\t');
            else if (esc == 'f') outText.push_back('\f');
            else if (esc == '"' || esc == '\\') outText.push_back(esc);
            else {
                outText.push_back(esc);
            }
        } else {
            outText.push_back(c);
        }
    }
    outErr = "unterminated string literal in pattern";
    return false;
}

std::string readIdentifier(std::string_view s, size_t& i) {
    size_t start = i;
    while (i < s.size() && isIdentChar(s[i])) {
        ++i;
    }
    return std::string(s.substr(start, i - start));
}

bool isUnderlineLine(std::string_view nextLine, char underlineChar, size_t titleLen) noexcept {
    const std::string trimmed = util::trimTrailingWhitespace(nextLine);
    if (trimmed.size() < 3 || trimmed.size() < titleLen) {
        return false;
    }
    for (char c : trimmed) {
        if (c != underlineChar) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool compilePattern(std::string_view src, CompiledPattern& outPattern, std::string& outError) {
    outPattern.source = std::string(src);
    outPattern.elements.clear();
    outPattern.consumesNextLineUnderline = false;

    size_t i = 0;
    skipSpaces(src, i);
    if (i >= src.size()) {
        outError = "pattern cannot be empty";
        return false;
    }

    while (i < src.size()) {
        PatternElement elem;
        if (src[i] == '"') {
            elem.kind = ElementKind::Literal;
            if (!parseQuotedLiteral(src, i, elem.textArg, outError)) {
                return false;
            }
            if (elem.textArg.empty()) {
                outError = "empty literal \"\" is not allowed in pattern";
                return false;
            }
        } else {
            const std::string kw = readIdentifier(src, i);
            if (kw.empty()) {
                outError = "unexpected character '" + std::string(1, src[i]) + "' in pattern";
                return false;
            }

            if (kw == "ws") {
                elem.kind = ElementKind::Ws;
            } else if (kw == "tab") {
                elem.kind = ElementKind::Tab;
            } else if (kw == "version") {
                elem.kind = ElementKind::Version;
            } else if (kw == "number") {
                elem.kind = ElementKind::Number;
            } else if (kw == "any") {
                elem.kind = ElementKind::Any;
            } else if (kw == "after_blank") {
                elem.kind = ElementKind::AfterBlank;
            } else if (kw == "underline") {
                elem.kind = ElementKind::Underline;
                if (i >= src.size() || src[i] != '(') {
                    outError = "expected '(' after underline";
                    return false;
                }
                ++i;
                skipSpaces(src, i);
                if (!parseQuotedLiteral(src, i, elem.textArg, outError)) {
                    return false;
                }
                if (elem.textArg.size() != 1) {
                    outError = "underline(\"c\") requires a single underline character";
                    return false;
                }
                skipSpaces(src, i);
                if (i >= src.size() || src[i] != ')') {
                    outError = "expected ')' after underline(\"...\")";
                    return false;
                }
                ++i;
                outPattern.consumesNextLineUnderline = true;
            } else if (kw == "spaces") {
                elem.kind = ElementKind::Spaces;
                if (i >= src.size() || src[i] != '(') {
                    outError = "expected '(' after spaces";
                    return false;
                }
                ++i;
                skipSpaces(src, i);
                int n = 0;
                bool hasDigits = false;
                while (i < src.size() && src[i] >= '0' && src[i] <= '9') {
                    hasDigits = true;
                    n = n * 10 + (src[i] - '0');
                    ++i;
                }
                skipSpaces(src, i);
                if (!hasDigits || n <= 0 || i >= src.size() || src[i] != ')') {
                    outError = "spaces(N) requires a positive integer N followed by ')'";
                    return false;
                }
                ++i;  // skip ')'
                elem.countArg = n;
            } else if (kw == "date") {
                elem.kind = ElementKind::Date;
                if (i >= src.size() || src[i] != '(') {
                    outError = "expected '(' after date";
                    return false;
                }
                ++i;
                size_t closeParen = src.find(')', i);
                if (closeParen == std::string_view::npos) {
                    outError = "missing ')' in date(FORMAT)";
                    return false;
                }
                std::string_view fmtStr = src.substr(i, closeParen - i);
                if (!compileDateFormat(fmtStr, elem.dateFormat, outError)) {
                    return false;
                }
                i = closeParen + 1;
            } else if (kw == "until") {
                elem.kind = ElementKind::Until;
                if (i >= src.size() || src[i] != '(') {
                    outError = "expected '(' after until";
                    return false;
                }
                ++i;
                skipSpaces(src, i);
                if (!parseQuotedLiteral(src, i, elem.textArg, outError)) {
                    return false;
                }
                if (elem.textArg.empty()) {
                    outError = "until(\"...\") target cannot be empty";
                    return false;
                }
                skipSpaces(src, i);
                if (i >= src.size() || src[i] != ')') {
                    outError = "expected ')' after until(\"...\")";
                    return false;
                }
                ++i;
            } else {
                outError = "unknown pattern element '" + kw + "'";
                return false;
            }
        }

        // Check for optional `as NAME` capture modifier
        skipSpaces(src, i);
        size_t saved = i;
        const std::string maybeAs = readIdentifier(src, i);
        if (maybeAs == "as") {
            skipSpaces(src, i);
            const std::string capName = readIdentifier(src, i);
            if (capName.empty()) {
                outError = "expected capture name after 'as'";
                return false;
            }
            elem.captureName = capName;
            skipSpaces(src, i);
        } else {
            i = saved;
        }

        outPattern.elements.push_back(std::move(elem));
    }

    return true;
}

MatchResult matchLine(const CompiledPattern& pattern,
                      std::string_view line,
                      const MatchContext& ctx) {
    MatchResult res;
    size_t pos = 0;

    for (const auto& elem : pattern.elements) {
        const size_t startPos = pos;
        switch (elem.kind) {
            case ElementKind::Literal: {
                if (pos + elem.textArg.size() > line.size() ||
                    line.substr(pos, elem.textArg.size()) != elem.textArg) {
                    return res;
                }
                pos += elem.textArg.size();
                break;
            }
            case ElementKind::Ws: {
                if (pos >= line.size() || !isSpaceOrTab(line[pos])) {
                    return res;
                }
                while (pos < line.size() && isSpaceOrTab(line[pos])) {
                    ++pos;
                }
                break;
            }
            case ElementKind::Tab: {
                if (pos >= line.size() || line[pos] != '\t') {
                    return res;
                }
                ++pos;
                break;
            }
            case ElementKind::Spaces: {
                const size_t n = static_cast<size_t>(elem.countArg);
                if (pos + n > line.size()) {
                    return res;
                }
                for (size_t k = 0; k < n; ++k) {
                    if (line[pos + k] != ' ') {
                        return res;
                    }
                }
                pos += n;
                break;
            }
            case ElementKind::Date: {
                ParsedDate pd;
                if (!matchDateAt(elem.dateFormat, line, pos, pd)) {
                    return res;
                }
                break;
            }
            case ElementKind::Version: {
                if (!matchVersionAt(line, pos, nullptr)) {
                    return res;
                }
                break;
            }
            case ElementKind::Number: {
                if (pos >= line.size() || line[pos] < '0' || line[pos] > '9') {
                    return res;
                }
                while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
                    ++pos;
                }
                break;
            }
            case ElementKind::Until: {
                const size_t found = line.find(elem.textArg, pos);
                if (found == std::string_view::npos) {
                    return res;
                }
                pos = found;
                break;
            }
            case ElementKind::Any: {
                // If preceded only by after_blank / spaces(N) (no literal/token matched),
                // require at least one non-blank character so blank/whitespace-only lines don't match.
                bool hasConcretePrefix = false;
                for (const auto& prev : pattern.elements) {
                    if (&prev == &elem) break;
                    if (prev.kind != ElementKind::AfterBlank &&
                        prev.kind != ElementKind::Spaces &&
                        prev.kind != ElementKind::Ws &&
                        prev.kind != ElementKind::Tab) {
                        hasConcretePrefix = true;
                        break;
                    }
                }
                if (!hasConcretePrefix) {
                    bool allBlank = true;
                    for (size_t k = pos; k < line.size(); ++k) {
                        if (!isSpaceOrTab(line[k])) {
                            allBlank = false;
                            break;
                        }
                    }
                    if (allBlank) {
                        return res;
                    }
                }
                pos = line.size();
                break;
            }
            case ElementKind::AfterBlank: {
                if (!ctx.precededByBlank) {
                    return res;
                }
                break;
            }
            case ElementKind::Underline: {
                // Title line must be non-blank, not start with spaces/dots (`..`), and not itself be an underline
                const std::string trimmedTitle = util::trimTrailingWhitespace(line.substr(pos));
                if (trimmedTitle.empty() || isSpaceOrTab(trimmedTitle[0]) ||
                    util::startsWith(trimmedTitle, "..")) {
                    return res;
                }
                const char uChar = elem.textArg[0];
                bool allSameAsUnderline = true;
                for (char c : trimmedTitle) {
                    if (c != uChar) {
                        allSameAsUnderline = false;
                        break;
                    }
                }
                if (allSameAsUnderline) {
                    return res;
                }
                if (!ctx.hasNextLine || !isUnderlineLine(ctx.nextLineText, uChar, trimmedTitle.size())) {
                    return res;
                }
                pos = line.size();
                res.consumedNextLine = true;
                break;
            }
        }

        if (!elem.captureName.empty()) {
            res.captures[elem.captureName] =
                std::string(line.substr(startPos, pos - startPos));
        }
    }

    res.matched = true;
    return res;
}

}  // namespace docmerge::pattern
