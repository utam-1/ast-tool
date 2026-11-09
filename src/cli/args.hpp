#pragma once

#include <string>
#include <vector>

namespace docmerge::cli {

struct ParsedArgs {
    std::string command;                 // "merge", "resolve", "check", "dump", "init", "install", "help", "version"
    std::vector<std::string> positional; // Command-specific positional arguments
    bool explain = false;                // --explain
    bool dryRun = false;                 // --dry-run
    std::string configPath = ".docmerge.toml"; // --config <path>
    bool valid = true;
    std::string errorMessage;
};

ParsedArgs parseCommandLine(int argc, char** argv);

}  // namespace docmerge::cli

