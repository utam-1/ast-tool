#include "tree_printer.hpp"
#include <ostream>

namespace {


bool visible(TSNode n, const PrintOptions& o) {
    return o.showAnonymous
        || ts_node_is_named(n)
        || ts_node_is_missing(n)
        || ts_node_is_error(n);
}


bool hasVisibleNext(TSNode n, const PrintOptions& o) {
    for (TSNode s = ts_node_next_sibling(n); !ts_node_is_null(s); s = ts_node_next_sibling(s)) {
        if (visible(s, o)) return true;
    }
    return false;
}


std::string snippet(const std::string& src, TSNode n) {
    uint32_t s = ts_node_start_byte(n);
    uint32_t e = ts_node_end_byte(n);
    std::string out;
    for (uint32_t i = s; i < e; ++i) {
        char ch = src[i];
        if (ch == '\n')      out += "\\n";
        else if (ch == '\t') out += "\\t";
        else                 out += ch;
    }
    if (out.size() > 30) out = out.substr(0, 27) + "...";
    return out;
}

void walk(TSTreeCursor* cur, const std::string& src, std::ostream& out,
          const PrintOptions& opt, const std::string& prefix,
          bool isRoot, bool isLast) {
    TSNode node = ts_tree_cursor_current_node(cur);
    const char* field = ts_tree_cursor_current_field_name(cur);

    out << prefix << (isRoot ? "" : (isLast ? "└── " : "├── "));
    if (field) out << field << ": ";
    out << ts_node_type(node);
    if (ts_node_is_missing(node)) out << " [MISSING]";
    if (ts_node_is_error(node))   out << " [ERROR]";
    out << " [" << ts_node_start_byte(node) << ".." << ts_node_end_byte(node) << ")";
    if (ts_node_child_count(node) == 0) {
        out << " \"" << snippet(src, node) << "\"";
    }
    out << "\n";

    const std::string childPrefix = prefix + (isRoot ? "" : (isLast ? "    " : "│   "));

    if (ts_tree_cursor_goto_first_child(cur)) {
        do {
            TSNode child = ts_tree_cursor_current_node(cur);
            if (!visible(child, opt)) continue;   
            walk(cur, src, out, opt, childPrefix, false, !hasVisibleNext(child, opt));
        } while (ts_tree_cursor_goto_next_sibling(cur));
        ts_tree_cursor_goto_parent(cur);          
    }
}

}

void PrintTree(TSNode root, const std::string& source,
               std::ostream& out, const PrintOptions& opts) {
    TSTreeCursor cur = ts_tree_cursor_new(root);
    walk(&cur, source, out, opts, "", true, true);
    ts_tree_cursor_delete(&cur);                  
}