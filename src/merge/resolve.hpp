#pragma once

#include <string>
#include <string_view>

namespace docmerge::merge {

struct SplitConflictFileResult {
    bool ok = true;
    bool hadMarkers = false;
    bool missingDiff3Base = false;
    std::string baseBytes;
    std::string oursBytes;
    std::string theirsBytes;
    int detectedMarkerWidth = 7;
    std::string errorMessage;
};

// Reconstructs `base`, `ours`, and `theirs` full file contents from a file
// containing diff3-style conflict markers (Section 9.1).
SplitConflictFileResult splitConflictedFile(std::string_view rawBytes,
                                            std::string_view filePathForError);

}  // namespace docmerge::merge

