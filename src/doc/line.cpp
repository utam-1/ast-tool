#include "doc/line.hpp"

#include "util/hash.hpp"

namespace docmerge::doc {

bool Line::isBlank() const noexcept {
    for (char c : text) {
        if (c != ' ' && c != '\t') {
            return false;
        }
    }
    return true;
}

Line makeLine(std::string text, std::string eol) {
    Line l;
    l.hash = util::fnv1a64(text);
    l.text = std::move(text);
    l.eol = std::move(eol);
    return l;
}

SplitDocumentLines splitLines(std::string_view rawBytes) {
    SplitDocumentLines result;
    if (rawBytes.size() >= 3 &&
        static_cast<unsigned char>(rawBytes[0]) == 0xEF &&
        static_cast<unsigned char>(rawBytes[1]) == 0xBB &&
        static_cast<unsigned char>(rawBytes[2]) == 0xBF) {
        result.hasBom = true;
        rawBytes.remove_prefix(3);
    }

    size_t lfCount = 0;
    size_t crlfCount = 0;

    size_t pos = 0;
    while (pos < rawBytes.size()) {
        size_t nl = rawBytes.find('\n', pos);
        if (nl == std::string_view::npos) {
            std::string text(rawBytes.substr(pos));
            result.lines.push_back(makeLine(std::move(text), ""));
            break;
        }

        if (nl > pos && rawBytes[nl - 1] == '\r') {
            std::string text(rawBytes.substr(pos, nl - 1 - pos));
            result.lines.push_back(makeLine(std::move(text), "\r\n"));
            ++crlfCount;
        } else {
            std::string text(rawBytes.substr(pos, nl - pos));
            result.lines.push_back(makeLine(std::move(text), "\n"));
            ++lfCount;
        }
        pos = nl + 1;
    }

    result.eolStyle = (crlfCount > lfCount) ? "\r\n" : "\n";
    return result;
}

std::string joinLines(const std::vector<Line>& lines, bool hasBom) {
    size_t totalBytes = hasBom ? 3 : 0;
    for (const auto& l : lines) {
        totalBytes += l.text.size() + l.eol.size();
    }

    std::string out;
    out.reserve(totalBytes);
    if (hasBom) {
        out.append("\xEF\xBB\xBF", 3);
    }
    for (const auto& l : lines) {
        out.append(l.text);
        out.append(l.eol);
    }
    return out;
}

}  // namespace docmerge::doc
