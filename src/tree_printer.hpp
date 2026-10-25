#pragma once
#include<tree_sitter/api.h>
#include<iosfwd>
#include<string>

struct PrintOptions {
    bool showAnonymous = false;
};

void PrintTree(TSNode node, const std::string& source, std::ostream& out, const PrintOptions& opts = PrintOptions{} );