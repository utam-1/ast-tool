#include "util/strings.hpp"

namespace docmerge::util {

namespace {

bool isSpaceOrTab(char c) noexcept {
    return c == ' ' || c == '\t';
}

}  // namespace

std::string trimTrailingWhitespace(std::string_view s) {
    size_t end = s.size();
    while (end > 0 && isSpaceOrTab(s[end - 1])) {
        --end;
    }
    return std::string(s.substr(0, end));
}

std::string trimWhitespace(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && isSpaceOrTab(s[start])) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && isSpaceOrTab(s[end - 1])) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

std::string collapseWhitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool inWs = false;
    for (char c : s) {
        if (isSpaceOrTab(c)) {
            if (!inWs) {
                out.push_back(' ');
                inWs = true;
            }
        } else {
            out.push_back(c);
            inWs = false;
        }
    }
    return out;
}

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string normalizeKey(std::string_view input,
                         const std::vector<std::string>& rules) {
    std::string result(input);
    for (const std::string& rule : rules) {
        if (rule == "trim-trailing") {
            result = trimTrailingWhitespace(result);
        } else if (rule == "collapse-ws") {
            result = collapseWhitespace(result);
        } else if (rule == "ignore-case") {
            result = toLowerAscii(result);
        }
    }
    return result;
}

bool startsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
}

}  // namespace docmerge::util
