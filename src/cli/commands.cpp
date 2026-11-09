#include "cli/commands.hpp"

#include "cli/init_infer.hpp"
#include "config/config.hpp"
#include "doc/doc_parse.hpp"
#include "doc/doc_write.hpp"
#include "doc/line.hpp"
#include "merge/conflict.hpp"
#include "merge/diff3.hpp"
#include "merge/resolve.hpp"
#include "merge/tree_merge.hpp"
#include "pattern/date.hpp"
#include "pattern/version.hpp"
#include "util/fileio.hpp"
#include "util/strings.hpp"
#include "verify/invariants.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

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

int executePlainFallbackMerge(const doc::SplitDocumentLines& baseSplit,
                              const doc::SplitDocumentLines& oursSplit,
                              const doc::SplitDocumentLines& theirsSplit,
                              int markerWidth,
                              merge::ConflictStyle conflictStyle,
                              const std::string& targetWritePath,
                              bool dryRun,
                              std::ostream& out,
                              std::ostream& err) {
    const merge::Diff3Result merged =
        merge::diff3Merge(baseSplit.lines, oursSplit.lines, theirsSplit.lines);

    merge::ConflictMarkerConfig markerCfg;
    markerCfg.style = conflictStyle;
    markerCfg.markerWidth = markerWidth;
    markerCfg.eol = oursSplit.eolStyle;

    const std::vector<doc::Line> outputLines = merged.toLines(markerCfg);
    const bool preserveBom = oursSplit.hasBom || baseSplit.hasBom;
    const std::string outputBytes = doc::joinLines(outputLines, preserveBom);

    if (dryRun) {
        out << outputBytes;
    } else {
        std::string ioErr;
        if (!util::writeFileAtomic(targetWritePath, outputBytes, ioErr)) {
            err << "docmerge: " << ioErr << "\n";
            return 2;
        }
    }
    return merged.hasConflicts ? 1 : 0;
}

int performThreeWayEngine(std::string_view baseRaw,
                          std::string_view oursRaw,
                          std::string_view theirsRaw,
                          const std::string& targetWritePath,
                          const std::string& logicalPath,
                          int markerWidth,
                          const ParsedArgs& args,
                          std::ostream& out,
                          std::ostream& err) {
    if (util::containsNulByte(baseRaw) ||
        util::containsNulByte(oursRaw) ||
        util::containsNulByte(theirsRaw)) {
        err << "docmerge: binary file detected for '" << logicalPath
            << "'; falling back to conflict.\n";
        return 1;
    }

    const doc::SplitDocumentLines baseSplit = doc::splitLines(baseRaw);
    const doc::SplitDocumentLines oursSplit = doc::splitLines(oursRaw);
    const doc::SplitDocumentLines theirsSplit = doc::splitLines(theirsRaw);

    const std::string cfgPath = config::discoverConfigPath(args.configPath, logicalPath);
    config::Config cfg;
    std::string cfgErr;
    const config::DocRule* docRule = nullptr;

    if (std::filesystem::exists(cfgPath)) {
        if (!config::loadConfigFile(cfgPath, cfg, cfgErr)) {
            err << "docmerge: warning: invalid config '" << cfgPath << "' (" << cfgErr
                << "); falling back to plain text 3-way merge.\n";
        } else {
            docRule = cfg.findDocForPath(logicalPath);
            if (docRule == nullptr) {
                // Also try matching by filename if logicalPath was a temporary or absolute path
                const std::string baseName =
                    std::filesystem::path(logicalPath).filename().string();
                docRule = cfg.findDocForPath(baseName);
            }
            if (docRule == nullptr && args.explain) {
                err << "[explain] no [[doc]] rule matched path '" << logicalPath
                    << "'; using plain text 3-way merge.\n";
            }
        }
    } else if (args.explain) {
        err << "[explain] config '" << cfgPath
            << "' not found; using plain text 3-way merge.\n";
    }

    if (docRule == nullptr) {
        return executePlainFallbackMerge(baseSplit,
                                         oursSplit,
                                         theirsSplit,
                                         markerWidth,
                                         merge::ConflictStyle::Diff3,
                                         targetWritePath,
                                         args.dryRun,
                                         out,
                                         err);
    }

    if (args.explain) {
        err << "[explain] using config '" << cfgPath << "' for '" << logicalPath << "'\n";
    }

    const doc::Document baseDoc = doc::parseDocumentLines(baseSplit, *docRule, nullptr);
    const doc::Document oursDoc = doc::parseDocumentLines(oursSplit, *docRule, nullptr);
    const doc::Document theirsDoc = doc::parseDocumentLines(theirsSplit, *docRule, nullptr);

    merge::ConflictMarkerConfig markerCfg;
    markerCfg.style = docRule->conflictStyle;
    markerCfg.markerWidth = markerWidth;
    markerCfg.eol = oursDoc.eolStyle;

    std::ostream* explainStream = args.explain ? &err : nullptr;
    const merge::TreeMergeResult treeMerged = merge::mergeDocumentTrees(
        baseDoc, oursDoc, theirsDoc, *docRule, markerCfg, explainStream);

    if (treeMerged.hasConflicts && docRule->onConflict == config::OnConflictPolicy::Fail) {
        err << "docmerge: conflict detected in '" << logicalPath
            << "' and on_conflict = \"fail\"; aborting merge.\n";
        return 1;
    }

    const std::string mergedBytes = doc::writeDocument(treeMerged.document, markerCfg);

    // Run safety verification (Section 7.8)
    const verify::VerificationResult ver = verify::verifyMergedOutput(
        baseDoc,
        oursDoc,
        theirsDoc,
        treeMerged.document,
        mergedBytes,
        treeMerged.hasConflicts,
        *docRule,
        markerCfg);

    if (!ver.ok) {
        err << "docmerge: warning: post-merge verification failed (" << ver.failureReason
            << "); discarding tree merge and falling back to plain text 3-way merge.\n";
        return executePlainFallbackMerge(baseSplit,
                                         oursSplit,
                                         theirsSplit,
                                         markerWidth,
                                         docRule->conflictStyle,
                                         targetWritePath,
                                         args.dryRun,
                                         out,
                                         err);
    }

    if (args.dryRun) {
        out << mergedBytes;
    } else {
        std::string ioErr;
        if (!util::writeFileAtomic(targetWritePath, mergedBytes, ioErr)) {
            err << "docmerge: " << ioErr << "\n";
            return 2;
        }
    }

    return treeMerged.hasConflicts ? 1 : 0;
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

    return performThreeWayEngine(
        baseRaw, oursRaw, theirsRaw, oursPath, logicalPath, markerWidth, args, out, err);
}

int runResolveCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    if (args.positional.empty()) {
        err << "docmerge resolve: expected FILE\n";
        return 2;
    }
    const std::string& filePath = args.positional[0];
    std::string raw;
    std::string ioErr;
    if (!util::readFileBytes(filePath, raw, ioErr)) {
        err << "docmerge resolve: " << ioErr << "\n";
        return 2;
    }

    const merge::SplitConflictFileResult splitRes = merge::splitConflictedFile(raw, filePath);
    if (!splitRes.ok) {
        err << "docmerge resolve: " << splitRes.errorMessage << "\n";
        return splitRes.missingDiff3Base ? 1 : 2;
    }
    if (!splitRes.hadMarkers) {
        if (args.explain) {
            err << "[explain] no conflict markers found in '" << filePath << "'\n";
        }
        if (args.dryRun) {
            out << raw;
        }
        return 0;
    }

    return performThreeWayEngine(splitRes.baseBytes,
                                 splitRes.oursBytes,
                                 splitRes.theirsBytes,
                                 filePath,
                                 filePath,
                                 splitRes.detectedMarkerWidth,
                                 args,
                                 out,
                                 err);
}

void tallyBlocksAndCheckOrder(const doc::Block& node,
                              const config::DocRule& docRule,
                              std::map<std::string, size_t>& countsByLevel,
                              std::vector<std::string>& orderWarnings) {
    if (node.level >= 1) {
        ++countsByLevel[node.levelName];
    }

    // Check child ordering if children belong to an ordered level
    if (node.children.size() >= 2) {
        const int childLvl = node.children.front().level;
        if (childLvl >= 1 && static_cast<size_t>(childLvl) <= docRule.levels.size()) {
            const auto& rule = docRule.levels[static_cast<size_t>(childLvl - 1)];
            if (rule.order != config::OrderMode::None) {
                for (size_t i = 1; i < node.children.size(); ++i) {
                    const auto& prev = node.children[i - 1];
                    const auto& curr = node.children[i];
                    if (rule.order == config::OrderMode::DateDesc ||
                        rule.order == config::OrderMode::DateAsc) {
                        auto itP = prev.captures.find(rule.sortBy);
                        auto itC = curr.captures.find(rule.sortBy);
                        if (itP != prev.captures.end() && itC != curr.captures.end()) {
                            int dP = pattern::parseDateStringToInt(itP->second);
                            int dC = pattern::parseDateStringToInt(itC->second);
                            if (dP > 0 && dC > 0) {
                                bool bad = (rule.order == config::OrderMode::DateDesc)
                                               ? (dC > dP)
                                               : (dC < dP);
                                if (bad) {
                                    orderWarnings.push_back(
                                        "out-of-order " + rule.name + ": \"" + curr.keyText +
                                        "\" after \"" + prev.keyText + "\"");
                                }
                            }
                        }
                    } else if (rule.order == config::OrderMode::VersionDesc ||
                               rule.order == config::OrderMode::VersionAsc) {
                        auto itP = prev.captures.find(rule.sortBy);
                        auto itC = curr.captures.find(rule.sortBy);
                        if (itP != prev.captures.end() && itC != curr.captures.end()) {
                            int cmp = pattern::compareVersionStrings(itC->second, itP->second);
                            bool bad = (rule.order == config::OrderMode::VersionDesc)
                                           ? (cmp > 0)
                                           : (cmp < 0);
                            if (bad) {
                                orderWarnings.push_back(
                                    "out-of-order " + rule.name + ": \"" + curr.keyText +
                                    "\" after \"" + prev.keyText + "\"");
                            }
                        }
                    }
                }
            }
        }
    }

    for (const auto& c : node.children) {
        tallyBlocksAndCheckOrder(c, docRule, countsByLevel, orderWarnings);
    }
}

int runCheckCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    const std::string targetHint = args.positional.empty() ? "" : args.positional[0];
    const std::string cfgPath = config::discoverConfigPath(args.configPath, targetHint);

    config::Config cfg;
    std::string cfgErr;
    if (!config::loadConfigFile(cfgPath, cfg, cfgErr)) {
        err << "docmerge check: " << cfgPath << ":" << cfgErr << "\n";
        return 1;
    }

    out << "Config '" << cfgPath << "' is valid (" << cfg.docs.size() << " [[doc]] rule(s)).\n";

    for (const std::string& filePath : args.positional) {
        std::string raw;
        std::string ioErr;
        if (!util::readFileBytes(filePath, raw, ioErr)) {
            err << "docmerge check: " << ioErr << "\n";
            return 1;
        }
        const config::DocRule* rule = cfg.findDocForPath(filePath);
        if (rule == nullptr) {
            const std::string baseName = std::filesystem::path(filePath).filename().string();
            rule = cfg.findDocForPath(baseName);
        }
        if (rule == nullptr) {
            err << "docmerge check: no [[doc]] rule in '" << cfgPath << "' matches '" << filePath
                << "'\n";
            return 1;
        }

        std::vector<doc::ParseDiag> diags;
        const doc::Document parsed = doc::parseDocument(raw, *rule, &diags);
        std::map<std::string, size_t> counts;
        std::vector<std::string> orderWarnings;
        tallyBlocksAndCheckOrder(parsed.root, *rule, counts, orderWarnings);

        out << "File '" << filePath << "':";
        for (const auto& lvl : rule->levels) {
            out << " " << lvl.name << "=" << counts[lvl.name];
        }
        out << "\n";

        for (const auto& d : diags) {
            out << "  warning (line " << d.lineNumber << "): " << d.message << "\n";
        }
        for (const auto& ow : orderWarnings) {
            out << "  warning: " << ow << "\n";
        }
    }

    return 0;
}

int runDumpCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    if (args.positional.empty()) {
        err << "docmerge dump: expected FILE\n";
        return 2;
    }
    const std::string& filePath = args.positional[0];
    const std::string cfgPath = config::discoverConfigPath(args.configPath, filePath);

    config::Config cfg;
    std::string cfgErr;
    if (!config::loadConfigFile(cfgPath, cfg, cfgErr)) {
        err << "docmerge dump: " << cfgPath << ":" << cfgErr << "\n";
        return 1;
    }

    const config::DocRule* rule = cfg.findDocForPath(filePath);
    if (rule == nullptr) {
        const std::string baseName = std::filesystem::path(filePath).filename().string();
        rule = cfg.findDocForPath(baseName);
    }
    if (rule == nullptr) {
        err << "docmerge dump: no [[doc]] rule matches '" << filePath << "'\n";
        return 1;
    }

    std::string raw;
    std::string ioErr;
    if (!util::readFileBytes(filePath, raw, ioErr)) {
        err << "docmerge dump: " << ioErr << "\n";
        return 2;
    }

    const doc::Document parsed = doc::parseDocument(raw, *rule, nullptr);
    doc::dumpDocumentTree(parsed, out);
    return 0;
}

int runInitCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    if (args.positional.empty()) {
        err << "docmerge init: expected FILE\n";
        return 2;
    }
    const std::string& filePath = args.positional[0];
    std::string raw;
    std::string ioErr;
    if (!util::readFileBytes(filePath, raw, ioErr)) {
        err << "docmerge init: " << ioErr << "\n";
        return 2;
    }

    const InitInferenceResult inferred = inferConfigFromSample(filePath, raw);
    err << inferred.reportSummary;
    out << inferred.draftToml;
    return 0;
}

int runInstallCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    // 1. Configure .git/config merge driver via fixed git config invocations
    if (!args.dryRun) {
        int rc1 = std::system("git config merge.docmerge.name \"documentation-aware merge\"");
        int rc2 = std::system("git config merge.docmerge.driver \"docmerge merge %O %A %B %L %P\"");
        if (rc1 != 0 || rc2 != 0) {
            err << "docmerge install: failed to update .git/config (are you inside a git repository?)\n";
            return 1;
        }
    }

    out << "Configured Git merge driver 'docmerge' in .git/config:\n"
        << "  merge.docmerge.name   = \"documentation-aware merge\"\n"
        << "  merge.docmerge.driver = \"docmerge merge %O %A %B %L %P\"\n";

    // 2. If .docmerge.toml exists, suggest or update .gitattributes
    const std::string cfgPath = config::discoverConfigPath(args.configPath, "");
    config::Config cfg;
    std::string cfgErr;
    std::vector<std::string> attrLines;
    if (std::filesystem::exists(cfgPath) && config::loadConfigFile(cfgPath, cfg, cfgErr)) {
        for (const auto& d : cfg.docs) {
            for (const auto& p : d.paths) {
                attrLines.push_back(p + " merge=docmerge");
            }
        }
    } else {
        attrLines = {
            "NEWS merge=docmerge",
            "ChangeLog merge=docmerge",
            "CHANGELOG.md merge=docmerge"};
    }

    out << "\nRecommended .gitattributes lines for your repository:\n";
    for (const auto& l : attrLines) {
        out << "  " << l << "\n";
    }
    return 0;
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
    if (args.command == "resolve") {
        return runResolveCommand(args, out, err);
    }
    if (args.command == "check") {
        return runCheckCommand(args, out, err);
    }
    if (args.command == "dump") {
        return runDumpCommand(args, out, err);
    }
    if (args.command == "init") {
        return runInitCommand(args, out, err);
    }
    if (args.command == "install") {
        return runInstallCommand(args, out, err);
    }

    err << "docmerge: unknown command '" << args.command << "'\n";
    return 2;
}

}  // namespace docmerge::cli
