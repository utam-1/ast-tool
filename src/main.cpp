#include "cli/args.hpp"
#include "cli/commands.hpp"

#include <iostream>

int main(int argc, char** argv) {
    const docmerge::cli::ParsedArgs args = docmerge::cli::parseCommandLine(argc, argv);
    return docmerge::cli::runCommand(args, std::cout, std::cerr);
}
