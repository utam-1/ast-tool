#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace docmerge::doc {

struct Line {
    std::string text;      // Line content without the line ending
    std::string eol;       // "\n", "\r\n", or "" (for last line without trailing newline)
    uint64_t hash = 0;     // 64-bit FNV-1a hash of `text` for fast line equality checks

    bool isBlank() const noexcept;
    bool ContentEquals(const Line& other) const noexcept {
        return hash == other.hash && text == other.text;
    }
};

struct SplitDocumentLines {
    std::vector<Line> lines;
    std::string eolStyle = "\n";  // Majority line ending ("\n" or "\r\n")
    bool hasBom = false;          // True if file started with UTF-8 BOM (\xEF\xBB\xBF)
};

// Splits raw file bytes into a sequence of `Line` objects while preserving every byte.
SplitDocumentLines splitLines(std::string_view rawBytes);

// Joins a sequence of `Line` objects back into raw bytes (prepending UTF-8 BOM if `hasBom` is true).
std::string joinLines(const std::vector<Line>& lines, bool hasBom = false);

// Helper to construct a Line with its hash precomputed.
Line makeLine(std::string text, std::string eol = "\n");

}  // namespace docmerge::doc
