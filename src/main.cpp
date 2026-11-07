#include <iostream>
#include <string>

namespace {

void printUsage(std::ostream& out) {
    out << "Usage: docmerge <command> [options]\n\n"
        << "Commands:\n"
        << "  merge BASE OURS THEIRS [MARKER_SIZE] [PATH]  Run 3-way merge driver\n"
        << "  resolve FILE                                 Resolve conflicted file in place\n"
        << "  check [FILE...]                              Validate config and parse files\n"
        << "  dump FILE                                    Print parsed block tree\n"
        << "  init FILE                                    Infer draft .docmerge.toml\n"
        << "  install                                      Configure git merge driver\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(std::cerr);
        return 1;
    }

    const std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        printUsage(std::cout);
        return 0;
    }

    if (cmd == "--version" || cmd == "-v") {
        std::cout << "docmerge 0.1.0\n";
        return 0;
    }

    std::cerr << "docmerge: command '" << cmd << "' is not implemented yet.\n";
    return 1;
}
