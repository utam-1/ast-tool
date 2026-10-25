#pragma once
#include <tree_sitter/api.h>
#include <memory>
#include <string>

struct TreeDeleter {
    void operator()(TSTree* t) const { ts_tree_delete(t); }
};
struct ParserDeleter {
    void operator()(TSParser* p) const { ts_parser_delete(p); }
};

class ParsedFile {
public:
    static ParsedFile parse(const std::string& path);

    TSNode root() const { return ts_tree_root_node(tree_.get()); }
    const std::string& source() const { return source_; }

private:
    ParsedFile() = default;

    std::string source_;
    std::unique_ptr<TSTree, TreeDeleter> tree_;
};
