#include "util/glob.hpp"

#include <string>

namespace docmerge::util {

namespace {

std::string normalizePath(std::string_view p) {
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        out.push_back(c == '\\' ? '/' : c);
    }
    // Strip leading "./"
    while (out.size() >= 2 && out[0] == '.' && out[1] == '/') {
        out.erase(0, 2);
    }
    return out;
}

bool matchImpl(std::string_view pat, size_t pi, std::string_view str, size_t si) {
    while (pi < pat.size()) {
        if (pat[pi] == '*') {
            // Check for "**"
            if (pi + 1 < pat.size() && pat[pi + 1] == '*') {
                size_t nextPi = pi + 2;
                // "**/foo" can match "foo" directly (zero directory segments)
                if (nextPi < pat.size() && pat[nextPi] == '/') {
                    if (matchImpl(pat, nextPi + 1, str, si)) {
                        return true;
                    }
                }
                // Or "**" matches zero or more arbitrary characters (including '/')
                for (size_t k = si; k <= str.size(); ++k) {
                    if (matchImpl(pat, nextPi, str, k)) {
                        return true;
                    }
                }
                return false;
            }
            // Single '*': matches zero or more non-'/' characters
            size_t nextPi = pi + 1;
            for (size_t k = si; k <= str.size(); ++k) {
                if (matchImpl(pat, nextPi, str, k)) {
                    return true;
                }
                if (k < str.size() && str[k] == '/') {
                    break;
                }
            }
            return false;
        }

        if (si >= str.size()) {
            return false;
        }

        if (pat[pi] == '?') {
            if (str[si] == '/') {
                return false;
            }
            ++pi;
            ++si;
        } else {
            if (pat[pi] != str[si]) {
                return false;
            }
            ++pi;
            ++si;
        }
    }
    return si == str.size();
}

}  // namespace

bool globMatch(std::string_view pattern, std::string_view path) {
    const std::string normPat = normalizePath(pattern);
    const std::string normPath = normalizePath(path);
    return matchImpl(normPat, 0, normPath, 0);
}

bool globMatchAny(const std::vector<std::string>& patterns, std::string_view path) {
    for (const auto& pat : patterns) {
        if (globMatch(pat, path)) {
            return true;
        }
    }
    return false;
}

}  // namespace docmerge::util

