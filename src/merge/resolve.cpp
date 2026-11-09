#include "merge/resolve.hpp"

#include "doc/line.hpp"

#include <vector>

namespace docmerge::merge {

namespace {

size_t countRepeatedPrefix(std::string_view s, char ch) noexcept {
    size_t n = 0;
    while (n < s.size() && s[n] == ch) {
        ++n;
    }
    return n;
}

bool isMarkerLine(std::string_view text, char markerChar, size_t expectedWidth) noexcept {
    const size_t n = countRepeatedPrefix(text, markerChar);
    if (n != expectedWidth || n < 7) {
        return false;
    }
    if (markerChar == '=') {
        return text.size() == n;
    }
    return text.size() == n || text[n] == ' ';
}

}  // namespace

SplitConflictFileResult splitConflictedFile(std::string_view rawBytes,
                                            std::string_view filePathForError) {
    SplitConflictFileResult res;
    const doc::SplitDocumentLines split = doc::splitLines(rawBytes);

    enum class State {
        Common,
        InOurs,
        InBase,
        InTheirs
    };

    State state = State::Common;
    size_t currentWidth = 7;
    std::vector<doc::Line> baseLines;
    std::vector<doc::Line> oursLines;
    std::vector<doc::Line> theirsLines;

    for (size_t i = 0; i < split.lines.size(); ++i) {
        const doc::Line& L = split.lines[i];
        if (state == State::Common) {
            const size_t w = countRepeatedPrefix(L.text, '<');
            if (w >= 7 && (L.text.size() == w || L.text[w] == ' ')) {
                state = State::InOurs;
                currentWidth = w;
                res.detectedMarkerWidth = static_cast<int>(w);
                res.hadMarkers = true;
                continue;
            }
            baseLines.push_back(L);
            oursLines.push_back(L);
            theirsLines.push_back(L);
        } else if (state == State::InOurs) {
            if (isMarkerLine(L.text, '|', currentWidth)) {
                state = State::InBase;
            } else if (isMarkerLine(L.text, '=', currentWidth)) {
                res.ok = false;
                res.missingDiff3Base = true;
                res.errorMessage =
                    "conflict markers in '" + std::string(filePathForError) +
                    "' do not include base section (diff3 required).\n"
                    "Re-create conflict markers with base included:\n"
                    "  git checkout --conflict=diff3 -- " +
                    std::string(filePathForError);
                return res;
            } else {
                oursLines.push_back(L);
            }
        } else if (state == State::InBase) {
            if (isMarkerLine(L.text, '=', currentWidth)) {
                state = State::InTheirs;
            } else {
                baseLines.push_back(L);
            }
        } else if (state == State::InTheirs) {
            if (isMarkerLine(L.text, '>', currentWidth)) {
                state = State::Common;
            } else {
                theirsLines.push_back(L);
            }
        }
    }

    if (state != State::Common) {
        res.ok = false;
        res.errorMessage = "unterminated conflict marker in '" + std::string(filePathForError) + "'";
        return res;
    }

    res.baseBytes = doc::joinLines(baseLines, split.hasBom);
    res.oursBytes = doc::joinLines(oursLines, split.hasBom);
    res.theirsBytes = doc::joinLines(theirsLines, split.hasBom);
    return res;
}

}  // namespace docmerge::merge

