#include "cli/commands.hpp"

#include "doc/line.hpp"
#include "merge/conflict.hpp"
#include "merge/diff3.hpp"
#include "util/fileio.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace docmerge::cli {

namespace {

int parseMarkerWidth(const std::string& raw, int fallback = 7) {
    char* endPtr = nullptr;
    long val = std::strtol(raw.c_str(), &endPtr, 10);
    if (endPtr == raw.c_str() || *endPtr != '\0' || val <= 0 || val > 64) {
        return fallback;
    }
    return static_cast<int>(val);
}

int runMergeCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    if (args.positional.size() < 3) {
        err << "docmerge merge: expected BASE OURS THEIRS [MARKER_SIZE] [PATH]\n";
        return 2;
    }

    const std::string& basePath = args.positional[0];
    const std::string& oursPath = args.positional[1];
    const std::string& theirsPath = args.positional[2];
    const int markerWidth =
        args.positional.size() >= 4 ? parseMarkerWidth(args.positional[3], 7) : 7;
    const std::string logicalPath =
        args.positional.size() >= 5 ? args.positional[4] : oursPath;

    std::string baseRaw;
    std::string oursRaw;
    std::string theirsRaw;
    std::string ioErr;

    if (!util::readFileBytes(basePath, baseRaw, ioErr) ||
        !util::readFileBytes(oursPath, oursRaw, ioErr) ||
        !util::readFileBytes(theirsPath, theirsRaw, ioErr)) {
        err << "docmerge merge: " << ioErr << "\n";
        return 2;
    }

    if (util::containsNulByte(baseRaw) ||
        util::containsNulByte(oursRaw) ||
        util::containsNulByte(theirsRaw)) {
        err << "docmerge merge: binary file detected for '" << logicalPath
            << "'; leaving ours untouched and reporting conflict.\n";
        return 1;
    }

    const doc::SplitDocumentLines baseSplit = doc::splitLines(baseRaw);
    const doc::SplitDocumentLines oursSplit = doc::splitLines(oursRaw);
    const doc::SplitDocumentLines theirsSplit = doc::splitLines(theirsRaw);

    if (args.explain) {
        err << "[explain] path=" << logicalPath
            << " mode=text-diff3 (structured config merge not active or fallback)\n";
    }

    const merge::Diff3Result merged =
        merge::diff3Merge(baseSplit.lines, oursSplit.lines, theirsSplit.lines);

    merge::ConflictMarkerConfig markerCfg;
    markerCfg.style = merge::ConflictStyle::Diff3;
    markerCfg.markerWidth = markerWidth;
    markerCfg.eol = oursSplit.eolStyle;

    const std::vector<doc::Line> outputLines = merged.toLines(markerCfg);
    const bool preserveBom = oursSplit.hasBom || baseSplit.hasBom;
    const std::string outputBytes = doc::joinLines(outputLines, preserveBom);

    if (args.explain) {
        err << "[explain] chunks=" << merged.chunks.size()
            << " conflicts=" << merged.conflictCount << "\n";
    }

    if (args.dryRun) {
        out << outputBytes;
    } else {
        if (!util::writeFileAtomic(oursPath, outputBytes, ioErr)) {
            err << "docmerge merge: " << ioErr << "\n";
            return 2;
        }
    }

    return merged.hasConflicts ? 1 : 0;
}

}  // namespace

void printUsage(std::ostream& out) {
    out << "Usage: docmerge <command> [options]\n\n"
        << "Commands:\n"
        << "  merge BASE OURS THEIRS [MARKER_SIZE] [PATH]  Run 3-way merge driver\n"
        << "  resolve FILE                                 Resolve conflicted file in place\n"
        << "  check [FILE...]                              Validate config and parse files\n"
        << "  dump FILE                                    Print parsed block tree\n"
        << "  init FILE                                    Infer draft .docmerge.toml\n"
        << "  install                                      Configure git merge driver\n\n"
        << "Global options:\n"
        << "  --config PATH                                Path to .docmerge.toml\n"
        << "  --explain                                    Print merge decisions\n"
        << "  --dry-run                                    Print output without writing\n"
        << "  -h, --help                                   Show this help message\n"
        << "  -v, --version                                Show version\n";
}

int runCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    if (!args.valid) {
        err << "docmerge: " << args.errorMessage << "\n\n";
        printUsage(err);
        return 2;
    }

    if (args.command == "help") {
        printUsage(out);
        return 0;
    }
    if (args.command == "version") {
        out << "docmerge 0.1.0\n";
        return 0;
    }
    if (args.command == "merge") {
        return runMergeCommand(args, out, err);
    }

    err << "docmerge: command '" << args.command << "' is not implemented yet.\n";
    return 1;
}

}  // namespace docmerge::cli
