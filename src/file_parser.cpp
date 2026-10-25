#include "file_parser.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

extern "C" const TSLanguage* tree_sitter_cpp();

static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ParsedFile ParsedFile::parse(const std::string& path) {
    ParsedFile result;
    result.source_ = readFile(path);

    std::unique_ptr<TSParser, ParserDeleter> parser(ts_parser_new());
    if (!ts_parser_set_language(parser.get(), tree_sitter_cpp())) {
        throw std::runtime_error("Tree-sitter language version mismatch");
    }

    result.tree_.reset(ts_parser_parse_string(
        parser.get(), nullptr,
        result.source_.c_str(), static_cast<uint32_t>(result.source_.size())));

    if (!result.tree_) {
        throw std::runtime_error("Parsing failed for: " + path);
    }
    return result;
}
