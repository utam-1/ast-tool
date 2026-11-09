#include "cli/init_infer.hpp"

#include "doc/line.hpp"
#include "pattern/date.hpp"
#include "pattern/pattern.hpp"
#include "pattern/version.hpp"

#include <filesystem>
#include <sstream>
#include <vector>

namespace docmerge::cli {

namespace {

struct CandidatePattern {
    std::string levelName;
    std::string startPattern;
    std::string keySpec;
    std::string defaultInsert;
    bool hasDateCapture = false;
    bool hasVersionCapture = false;
    int hierarchyRank = 1;  // lower rank = outer level, higher rank = inner level
};

struct ScoredCandidate {
    CandidatePattern cand;
    size_t matchCount = 0;
    std::string inferredOrder = "none";
    size_t outOfOrderCount = 0;
};

}  // namespace

InitInferenceResult inferConfigFromSample(std::string_view filePath,
                                          std::string_view rawBytes) {
    InitInferenceResult res;
    const doc::SplitDocumentLines split = doc::splitLines(rawBytes);

    const std::vector<CandidatePattern> catalogue = {
        {"entry", "date(YYYY-MM-DD) as date ws any as who", "header", "top", true, false, 1},
        {"release", "\"Version \" version as version any", "header", "top", false, true, 1},
        {"release", "\"## [\" version as version \"]\" any", "header", "top", false, true, 1},
        {"release", "\"## \" version as version any", "header", "top", false, true, 1},
        {"release", "\"## \" any as title", "header", "top", false, false, 1},
        {"section", "underline(\"=\") as title", "header", "bottom", false, false, 1},
        {"section", "\"* \" any as title", "header", "bottom", false, false, 1},
        {"subsection", "underline(\"-\") as title", "header", "bottom", false, false, 2},
        {"category", "\"### \" any as title", "header", "bottom", false, false, 2},
        {"entry", "\"** \" any as title", "header", "bottom", false, false, 2},
        {"item", "tab \"* \" any", "text", "bottom", false, false, 3},
        {"item", "\"* \" any", "text", "bottom", false, false, 3},
        {"item", "\"- \" any", "text", "bottom", false, false, 3},
        {"item", "spaces(2) \"* \" any", "text", "bottom", false, false, 3},
        {"item", "spaces(2) \"- \" any", "text", "bottom", false, false, 3},
        {"item", "after_blank spaces(2) any", "text", "bottom", false, false, 3},
    };

    std::vector<ScoredCandidate> matched;
    std::vector<bool> lineClaimed(split.lines.size(), false);

    for (const auto& cand : catalogue) {
        pattern::CompiledPattern pat;
        std::string err;
        if (!pattern::compilePattern(cand.startPattern, pat, err)) {
            continue;
        }

        size_t count = 0;
        std::vector<int> dateVals;
        std::vector<std::string> verVals;
        std::vector<size_t> hitLines;

        for (size_t i = 0; i < split.lines.size(); ++i) {
            if (lineClaimed[i]) continue;
            pattern::MatchContext ctx;
            ctx.precededByBlank = (i > 0 && split.lines[i - 1].isBlank());
            if (i + 1 < split.lines.size()) {
                ctx.hasNextLine = true;
                ctx.nextLineText = split.lines[i + 1].text;
            }
            auto mr = pattern::matchLine(pat, split.lines[i].text, ctx);
            if (mr.matched) {
                ++count;
                hitLines.push_back(i);
                if (mr.consumedNextLine && i + 1 < split.lines.size()) {
                    hitLines.push_back(i + 1);
                    ++i;
                }
                if (cand.hasDateCapture) {
                    int d = pattern::parseDateStringToInt(mr.captures["date"]);
                    if (d > 0) dateVals.push_back(d);
                }
                if (cand.hasVersionCapture) {
                    verVals.push_back(mr.captures["version"]);
                }
            }
        }

        if (count >= 2) {
            bool rankTaken = false;
            for (const auto& existing : matched) {
                if (existing.cand.hierarchyRank == cand.hierarchyRank) {
                    rankTaken = true;
                    break;
                }
            }
            if (rankTaken) continue;

            for (size_t idx : hitLines) {
                lineClaimed[idx] = true;
            }

            ScoredCandidate sc;
            sc.cand = cand;
            sc.matchCount = count;

            if (cand.hasDateCapture && dateVals.size() >= 2) {
                size_t descViolations = 0;
                size_t ascViolations = 0;
                for (size_t k = 1; k < dateVals.size(); ++k) {
                    if (dateVals[k] > dateVals[k - 1]) ++descViolations;
                    if (dateVals[k] < dateVals[k - 1]) ++ascViolations;
                }
                if (descViolations <= ascViolations) {
                    sc.inferredOrder = "date-desc";
                    sc.outOfOrderCount = descViolations;
                } else {
                    sc.inferredOrder = "date-asc";
                    sc.outOfOrderCount = ascViolations;
                }
            } else if (cand.hasVersionCapture && verVals.size() >= 2) {
                size_t descViolations = 0;
                size_t ascViolations = 0;
                for (size_t k = 1; k < verVals.size(); ++k) {
                    int cmp = pattern::compareVersionStrings(verVals[k], verVals[k - 1]);
                    if (cmp > 0) ++descViolations;
                    if (cmp < 0) ++ascViolations;
                }
                if (descViolations <= ascViolations) {
                    sc.inferredOrder = "version-desc";
                    sc.outOfOrderCount = descViolations;
                } else {
                    sc.inferredOrder = "version-asc";
                    sc.outOfOrderCount = ascViolations;
                }
            }

            matched.push_back(std::move(sc));
        }
    }

    if (matched.empty()) {
        ScoredCandidate fallback;
        fallback.cand = {"entry", "\"## \" any as title", "header", "top", false, false, 1};
        fallback.matchCount = 0;
        matched.push_back(fallback);
    }

    std::error_code ec;
    std::string filename = std::filesystem::path(filePath).filename().string();
    if (filename.empty()) {
        filename = std::string(filePath);
    }

    std::ostringstream toml;
    toml << "version = 1\n\n"
         << "[[doc]]\n"
         << "paths = [\"" << filename << "\", \"**/" << filename << "\"]\n";

    std::ostringstream report;
    report << "docmerge init analysis for '" << filePath << "':\n";

    for (const auto& sc : matched) {
        toml << "\n  [[doc.level]]\n"
             << "  name   = \"" << sc.cand.levelName << "\"\n"
             << "  start  = '" << sc.cand.startPattern << "'\n"
             << "  key    = \"" << sc.cand.keySpec << "\"\n";
        if (sc.inferredOrder != "none") {
            toml << "  order  = \"" << sc.inferredOrder << "\"\n";
        } else {
            toml << "  insert = \"" << sc.cand.defaultInsert << "\"\n";
        }

        report << "  - level '" << sc.cand.levelName << "': found " << sc.matchCount
               << " blocks";
        if (sc.inferredOrder != "none") {
            report << " (order = " << sc.inferredOrder << ", " << sc.outOfOrderCount
                   << " out of order)";
        }
        report << "\n";
    }

    res.draftToml = toml.str();
    res.reportSummary = report.str();
    return res;
}

}  // namespace docmerge::cli
