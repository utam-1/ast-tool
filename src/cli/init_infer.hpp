#pragma once

#include <string>
#include <string_view>

namespace docmerge::cli {

struct InitInferenceResult {
    std::string draftToml;
    std::string reportSummary;
};

// Heuristic analysis of an existing documentation file (Section 9.2) that produces
// a draft `.docmerge.toml` and a human-readable summary report.
InitInferenceResult inferConfigFromSample(std::string_view filePath,
                                          std::string_view rawBytes);

}  // namespace docmerge::cli

