#pragma once

#include <string>
#include <string_view>

namespace docmerge::util {

// Reads an entire file in binary mode so all line endings and bytes are preserved.
bool readFileBytes(const std::string& path, std::string& outContent, std::string& outError);

// Atomically writes `content` to `path` by writing to a temporary file in the same
// directory and renaming it over `path`.
bool writeFileAtomic(const std::string& path, std::string_view content, std::string& outError);

// Returns true if the buffer contains any NUL ('\0') byte (indicating binary input).
bool containsNulByte(std::string_view content) noexcept;

}  // namespace docmerge::util
