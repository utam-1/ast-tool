#include "merge/conflict.hpp"

#include <algorithm>

namespace docmerge::merge {

namespace {

void appendBodyWithTrailingEol(std::vector<doc::Line>& out,
                               const std::vector<doc::Line>& section,
                               const std::string& defaultEol) {
    for (const auto& l : section) {
        out.push_back(l);
        if (out.back().eol.empty()) {
            out.back().eol = defaultEol;
        }
    }
}

}  // namespace

std::vector<doc::Line> formatConflictLines(
    const std::vector<doc::Line>& baseLines,
    const std::vector<doc::Line>& oursLines,
    const std::vector<doc::Line>& theirsLines,
    const ConflictMarkerConfig& cfg) {
    const size_t width = static_cast<size_t>(std::max(1, cfg.markerWidth));
    const std::string eol = cfg.eol.empty() ? "\n" : cfg.eol;

    std::vector<doc::Line> out;
    std::string startMarker(width, '<');
    if (!cfg.oursLabel.empty()) {
        startMarker += " " + cfg.oursLabel;
    }
    out.push_back(doc::makeLine(std::move(startMarker), eol));

    appendBodyWithTrailingEol(out, oursLines, eol);

    if (cfg.style == ConflictStyle::Diff3) {
        std::string baseMarker(width, '|');
        if (!cfg.baseLabel.empty()) {
            baseMarker += " " + cfg.baseLabel;
        }
        out.push_back(doc::makeLine(std::move(baseMarker), eol));
        appendBodyWithTrailingEol(out, baseLines, eol);
    }

    out.push_back(doc::makeLine(std::string(width, '='), eol));
    appendBodyWithTrailingEol(out, theirsLines, eol);

    std::string endMarker(width, '>');
    if (!cfg.theirsLabel.empty()) {
        endMarker += " " + cfg.theirsLabel;
    }
    out.push_back(doc::makeLine(std::move(endMarker), eol));

    return out;
}

}  // namespace docmerge::merge

