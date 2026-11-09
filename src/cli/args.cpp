#include "cli/args.hpp"

#include "util/strings.hpp"

namespace docmerge::cli {

ParsedArgs parseCommandLine(int argc, char** argv) {
    ParsedArgs out;
    if (argc < 2) {
        out.valid = false;
        out.errorMessage = "missing command";
        return out;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            out.command = "help";
            return out;
        }
        if (arg == "--version" || arg == "-v") {
            out.command = "version";
            return out;
        }
        if (arg == "--explain") {
            out.explain = true;
            continue;
        }
        if (arg == "--dry-run") {
            out.dryRun = true;
            continue;
        }
        if (arg == "--config") {
            if (i + 1 >= argc) {
                out.valid = false;
                out.errorMessage = "--config requires a path argument";
                return out;
            }
            out.configPath = argv[++i];
            continue;
        }
        if (util::startsWith(arg, "--config=")) {
            out.configPath = arg.substr(9);
            continue;
        }
        if (util::startsWith(arg, "-")) {
            out.valid = false;
            out.errorMessage = "unknown option: " + arg;
            return out;
        }

        if (out.command.empty()) {
            out.command = std::move(arg);
        } else {
            out.positional.push_back(std::move(arg));
        }
    }

    if (out.command.empty()) {
        out.valid = false;
        out.errorMessage = "missing command";
    }
    return out;
}

}  // namespace docmerge::cli

