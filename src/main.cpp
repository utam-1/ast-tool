#include "file_parser.hpp"
#include "tree_printer.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    PrintOptions opts;
    std::string path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--all") opts.showAnonymous = true;
        else path = arg;
    }
    if (path.empty()) {
        std::cerr << "Usage: ast_tool [--all] <file.cpp>\n";
        return 1;
    }

    try {
        ParsedFile file = ParsedFile::parse(path);
        PrintTree(file.root(), file.source(), std::cout, opts);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}