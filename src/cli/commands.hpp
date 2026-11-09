#pragma once

#include "cli/args.hpp"

#include <iosfwd>

namespace docmerge::cli {

void printUsage(std::ostream& out);

int runCommand(const ParsedArgs& args, std::ostream& out, std::ostream& err);

}  // namespace docmerge::cli
