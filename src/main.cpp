#include <tree_sitter/api.h>
#include <iostream>
#include <string>
#include <cstdlib>

// Linker ko batao ki file kahan par hai.
extern "C" const TSLanguage *tree_sitter_cpp();

int main (){
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser,tree_sitter_cpp());
    std :: string code = "int main() {return 0;}";
    TSTree* tree = ts_parser_parse_string(parser, nullptr, code.c_str(), (uint32_t)code.length());

    TSNode root = ts_tree_root_node(tree);

    char* s = ts_node_string(root);
    std::cout << s << std::endl;
    std::free(s);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 0;
}