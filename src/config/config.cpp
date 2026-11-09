#include "config/config.hpp"

#include "util/fileio.hpp"
#include "util/glob.hpp"
#include "util/strings.hpp"

#include <filesystem>
#include <set>
#include <sstream>

namespace docmerge::config {

namespace {

enum class ValueKind {
    String,
    Int,
    Bool,
    StringArray
};

struct TomlValue {
    ValueKind kind = ValueKind::String;
    std::string strVal;
    int intVal = 0;
    bool boolVal = false;
    std::vector<std::string> arrVal;
    int line = 1;
    int col = 1;
};

struct Cursor {
    std::string_view src;
    size_t pos = 0;
    int line = 1;
    int col = 1;

    bool eof() const noexcept { return pos >= src.size(); }
    char peek() const noexcept { return pos < src.size() ? src[pos] : '\0'; }

    char get() noexcept {
        if (pos >= src.size()) return '\0';
        char c = src[pos++];
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        return c;
    }

    std::string errAt(int l, int c, const std::string& msg) const {
        std::ostringstream oss;
        oss << l << ":" << c << ": " << msg;
        return oss.str();
    }

    std::string errHere(const std::string& msg) const {
        return errAt(line, col, msg);
    }
};

bool isHorizontalSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r';
}

bool isBareKeyChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

void skipHorizontalAndComments(Cursor& cur) noexcept {
    while (!cur.eof()) {
        char c = cur.peek();
        if (isHorizontalSpace(c)) {
            cur.get();
        } else if (c == '#') {
            while (!cur.eof() && cur.peek() != '\n') {
                cur.get();
            }
        } else {
            break;
        }
    }
}

void skipWhitespaceAndComments(Cursor& cur) noexcept {
    while (!cur.eof()) {
        char c = cur.peek();
        if (isHorizontalSpace(c) || c == '\n') {
            cur.get();
        } else if (c == '#') {
            while (!cur.eof() && cur.peek() != '\n') {
                cur.get();
            }
        } else {
            break;
        }
    }
}

bool parseBasicString(Cursor& cur, std::string& out, std::string& err) {
    const int startLine = cur.line;
    const int startCol = cur.col;
    cur.get();  // consume '"'
    out.clear();
    while (!cur.eof()) {
        char c = cur.peek();
        if (c == '\n' || c == '\r') {
            err = cur.errAt(startLine, startCol, "unterminated basic string literal");
            return false;
        }
        cur.get();
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (cur.eof()) {
                err = cur.errHere("unterminated escape sequence");
                return false;
            }
            char esc = cur.get();
            if (esc == '\\') out.push_back('\\');
            else if (esc == '"') out.push_back('"');
            else if (esc == 'n') out.push_back('\n');
            else if (esc == 't') out.push_back('\t');
            else if (esc == 'r') out.push_back('\r');
            else {
                err = cur.errHere(std::string("unsupported escape sequence '\\") + esc + "'");
                return false;
            }
        } else {
            out.push_back(c);
        }
    }
    err = cur.errAt(startLine, startCol, "unterminated basic string literal");
    return false;
}

bool parseLiteralString(Cursor& cur, std::string& out, std::string& err) {
    const int startLine = cur.line;
    const int startCol = cur.col;
    cur.get();  // consume '\''
    out.clear();
    while (!cur.eof()) {
        char c = cur.peek();
        if (c == '\n' || c == '\r') {
            err = cur.errAt(startLine, startCol, "unterminated literal string");
            return false;
        }
        cur.get();
        if (c == '\'') {
            return true;
        }
        out.push_back(c);
    }
    err = cur.errAt(startLine, startCol, "unterminated literal string");
    return false;
}

bool parseValue(Cursor& cur, TomlValue& outVal, std::string& err) {
    skipHorizontalAndComments(cur);
    outVal.line = cur.line;
    outVal.col = cur.col;
    if (cur.eof() || cur.peek() == '\n') {
        err = cur.errHere("expected value after '='");
        return false;
    }

    char c = cur.peek();
    if (c == '"') {
        outVal.kind = ValueKind::String;
        return parseBasicString(cur, outVal.strVal, err);
    }
    if (c == '\'') {
        outVal.kind = ValueKind::String;
        return parseLiteralString(cur, outVal.strVal, err);
    }
    if (c == '[') {
        cur.get();  // consume '['
        outVal.kind = ValueKind::StringArray;
        outVal.arrVal.clear();
        while (true) {
            skipWhitespaceAndComments(cur);
            if (cur.eof()) {
                err = cur.errAt(outVal.line, outVal.col, "unterminated array (expected ']')");
                return false;
            }
            if (cur.peek() == ']') {
                cur.get();
                return true;
            }
            std::string elem;
            if (cur.peek() == '"') {
                if (!parseBasicString(cur, elem, err)) return false;
            } else if (cur.peek() == '\'') {
                if (!parseLiteralString(cur, elem, err)) return false;
            } else {
                err = cur.errHere("expected string element in array");
                return false;
            }
            outVal.arrVal.push_back(std::move(elem));
            skipWhitespaceAndComments(cur);
            if (cur.peek() == ',') {
                cur.get();
            } else if (cur.peek() == ']') {
                cur.get();
                return true;
            } else {
                err = cur.errHere("expected ',' or ']' in array");
                return false;
            }
        }
    }

    // Read token for bool or int
    int tokLine = cur.line;
    int tokCol = cur.col;
    std::string tok;
    while (!cur.eof() && !isHorizontalSpace(cur.peek()) &&
           cur.peek() != '\n' && cur.peek() != '#') {
        tok.push_back(cur.get());
    }
    if (tok == "true" || tok == "false") {
        outVal.kind = ValueKind::Bool;
        outVal.boolVal = (tok == "true");
        return true;
    }

    // Parse integer
    size_t idx = 0;
    bool neg = false;
    if (!tok.empty() && (tok[0] == '-' || tok[0] == '+')) {
        neg = (tok[0] == '-');
        idx = 1;
    }
    if (idx >= tok.size()) {
        err = cur.errAt(tokLine, tokCol, "unexpected value '" + tok + "'");
        return false;
    }
    long val = 0;
    for (; idx < tok.size(); ++idx) {
        if (tok[idx] < '0' || tok[idx] > '9') {
            err = cur.errAt(tokLine, tokCol, "invalid value '" + tok + "' (expected string, int, bool, or array)");
            return false;
        }
        val = val * 10 + (tok[idx] - '0');
    }
    outVal.kind = ValueKind::Int;
    outVal.intVal = static_cast<int>(neg ? -val : val);
    return true;
}

bool parseOnConflict(const TomlValue& v, OnConflictPolicy& out, const Cursor& cur, std::string& err) {
    if (v.kind != ValueKind::String) {
        err = cur.errAt(v.line, v.col, "on_conflict must be a string (\"markers\" or \"fail\")");
        return false;
    }
    if (v.strVal == "markers") {
        out = OnConflictPolicy::Markers;
        return true;
    }
    if (v.strVal == "fail") {
        out = OnConflictPolicy::Fail;
        return true;
    }
    err = cur.errAt(v.line, v.col, "invalid on_conflict '" + v.strVal + "' (expected \"markers\" or \"fail\")");
    return false;
}

bool parseConflictStyle(const TomlValue& v, merge::ConflictStyle& out, const Cursor& cur, std::string& err) {
    if (v.kind != ValueKind::String) {
        err = cur.errAt(v.line, v.col, "conflict_style must be a string (\"diff3\" or \"merge\")");
        return false;
    }
    if (v.strVal == "diff3") {
        out = merge::ConflictStyle::Diff3;
        return true;
    }
    if (v.strVal == "merge") {
        out = merge::ConflictStyle::Merge;
        return true;
    }
    err = cur.errAt(v.line, v.col, "invalid conflict_style '" + v.strVal + "' (expected \"diff3\" or \"merge\")");
    return false;
}

enum class SectionKind {
    TopLevel,
    Defaults,
    Doc,
    DocLevel
};

}  // namespace

const DocRule* Config::findDocForPath(std::string_view repoRelativePath) const {
    for (const auto& doc : docs) {
        if (util::globMatchAny(doc.paths, repoRelativePath)) {
            return &doc;
        }
    }
    return nullptr;
}

bool parseConfigString(std::string_view tomlText, Config& outConfig, std::string& outError) {
    outConfig = Config{};
    // Strip UTF-8 BOM if present
    if (tomlText.size() >= 3 &&
        static_cast<unsigned char>(tomlText[0]) == 0xEF &&
        static_cast<unsigned char>(tomlText[1]) == 0xBB &&
        static_cast<unsigned char>(tomlText[2]) == 0xBF) {
        tomlText.remove_prefix(3);
    }

    Cursor cur{tomlText, 0, 1, 1};
    SectionKind section = SectionKind::TopLevel;
    bool hasVersion = false;

    while (true) {
        skipWhitespaceAndComments(cur);
        if (cur.eof()) {
            break;
        }

        if (cur.peek() == '[') {
            const int hdrLine = cur.line;
            const int hdrCol = cur.col;
            cur.get();  // first '['
            bool isArrayTable = false;
            if (cur.peek() == '[') {
                isArrayTable = true;
                cur.get();  // second '['
            }
            std::string tableName;
            while (!cur.eof() && cur.peek() != ']' && cur.peek() != '\n') {
                tableName.push_back(cur.get());
            }
            tableName = util::trimWhitespace(tableName);
            if (isArrayTable) {
                if (cur.get() != ']' || cur.get() != ']') {
                    outError = cur.errAt(hdrLine, hdrCol, "expected ']]' to close array of tables");
                    return false;
                }
                if (tableName == "doc") {
                    section = SectionKind::Doc;
                    DocRule d;
                    d.onConflict = outConfig.defaultOnConflict;
                    d.conflictStyle = outConfig.defaultConflictStyle;
                    outConfig.docs.push_back(std::move(d));
                } else if (tableName == "doc.level") {
                    if (outConfig.docs.empty()) {
                        outError = cur.errAt(hdrLine, hdrCol, "[[doc.level]] must appear after a [[doc]] table");
                        return false;
                    }
                    section = SectionKind::DocLevel;
                    LevelRule lvl;
                    lvl.levelIndex = static_cast<int>(outConfig.docs.back().levels.size() + 1);
                    outConfig.docs.back().levels.push_back(std::move(lvl));
                } else {
                    outError = cur.errAt(hdrLine, hdrCol, "unknown array table [[" + tableName + "]]");
                    return false;
                }
            } else {
                if (cur.get() != ']') {
                    outError = cur.errAt(hdrLine, hdrCol, "expected ']' to close table header");
                    return false;
                }
                if (tableName == "defaults") {
                    section = SectionKind::Defaults;
                } else {
                    outError = cur.errAt(hdrLine, hdrCol, "unknown table [" + tableName + "]");
                    return false;
                }
            }

            skipHorizontalAndComments(cur);
            if (!cur.eof() && cur.peek() != '\n') {
                outError = cur.errHere("unexpected characters after table header");
                return false;
            }
            continue;
        }

        // Parse key = value
        const int keyLine = cur.line;
        const int keyCol = cur.col;
        std::string key;
        while (!cur.eof() && isBareKeyChar(cur.peek())) {
            key.push_back(cur.get());
        }
        if (key.empty()) {
            outError = cur.errAt(keyLine, keyCol, "expected key or section header");
            return false;
        }

        skipHorizontalAndComments(cur);
        if (cur.eof() || cur.peek() != '=') {
            outError = cur.errHere("expected '=' after key '" + key + "'");
            return false;
        }
        cur.get();  // consume '='

        TomlValue val;
        if (!parseValue(cur, val, outError)) {
            return false;
        }

        skipHorizontalAndComments(cur);
        if (!cur.eof() && cur.peek() != '\n') {
            outError = cur.errHere("unexpected trailing characters after value");
            return false;
        }

        // Apply key-value according to current section
        if (section == SectionKind::TopLevel) {
            if (key == "version") {
                if (val.kind != ValueKind::Int || val.intVal != 1) {
                    outError = cur.errAt(val.line, val.col, "version must be integer 1");
                    return false;
                }
                outConfig.version = val.intVal;
                hasVersion = true;
            } else {
                outError = cur.errAt(keyLine, keyCol, "unknown top-level key '" + key + "'");
                return false;
            }
        } else if (section == SectionKind::Defaults) {
            if (key == "on_conflict") {
                if (!parseOnConflict(val, outConfig.defaultOnConflict, cur, outError)) return false;
            } else if (key == "conflict_style") {
                if (!parseConflictStyle(val, outConfig.defaultConflictStyle, cur, outError)) return false;
            } else {
                outError = cur.errAt(keyLine, keyCol, "unknown key '" + key + "' in [defaults]");
                return false;
            }
        } else if (section == SectionKind::Doc) {
            DocRule& doc = outConfig.docs.back();
            if (key == "paths") {
                if (val.kind != ValueKind::StringArray || val.arrVal.empty()) {
                    outError = cur.errAt(val.line, val.col, "paths must be a non-empty array of strings");
                    return false;
                }
                doc.paths = val.arrVal;
            } else if (key == "on_conflict") {
                if (!parseOnConflict(val, doc.onConflict, cur, outError)) return false;
            } else if (key == "conflict_style") {
                if (!parseConflictStyle(val, doc.conflictStyle, cur, outError)) return false;
            } else {
                outError = cur.errAt(keyLine, keyCol, "unknown key '" + key + "' in [[doc]]");
                return false;
            }
        } else if (section == SectionKind::DocLevel) {
            LevelRule& lvl = outConfig.docs.back().levels.back();
            if (key == "name") {
                if (val.kind != ValueKind::String || val.strVal.empty()) {
                    outError = cur.errAt(val.line, val.col, "level name must be a non-empty string");
                    return false;
                }
                lvl.name = val.strVal;
            } else if (key == "start") {
                if (val.kind != ValueKind::String) {
                    outError = cur.errAt(val.line, val.col, "start must be a pattern string");
                    return false;
                }
                lvl.startPatternRaw = val.strVal;
                std::string patErr;
                if (!pattern::compilePattern(val.strVal, lvl.startPattern, patErr)) {
                    outError = cur.errAt(val.line, val.col, "invalid start pattern: " + patErr);
                    return false;
                }
            } else if (key == "key") {
                if (val.kind != ValueKind::String ||
                    (val.strVal != "header" && val.strVal != "text" &&
                     !util::startsWith(val.strVal, "capture:"))) {
                    outError = cur.errAt(val.line, val.col,
                                         "key must be \"header\", \"text\", or \"capture:NAME\"");
                    return false;
                }
                lvl.keySpec = val.strVal;
            } else if (key == "order") {
                if (val.kind != ValueKind::String) {
                    outError = cur.errAt(val.line, val.col, "order must be a string");
                    return false;
                }
                if (val.strVal == "none") lvl.order = OrderMode::None;
                else if (val.strVal == "date-asc") lvl.order = OrderMode::DateAsc;
                else if (val.strVal == "date-desc") lvl.order = OrderMode::DateDesc;
                else if (val.strVal == "version-asc") lvl.order = OrderMode::VersionAsc;
                else if (val.strVal == "version-desc") lvl.order = OrderMode::VersionDesc;
                else if (val.strVal == "alpha-asc") lvl.order = OrderMode::AlphaAsc;
                else if (val.strVal == "alpha-desc") lvl.order = OrderMode::AlphaDesc;
                else {
                    outError = cur.errAt(val.line, val.col, "unknown order '" + val.strVal + "'");
                    return false;
                }
            } else if (key == "sort_by") {
                if (val.kind != ValueKind::String || val.strVal.empty()) {
                    outError = cur.errAt(val.line, val.col, "sort_by must be a non-empty string");
                    return false;
                }
                lvl.sortBy = val.strVal;
            } else if (key == "insert") {
                if (val.kind == ValueKind::String && val.strVal == "top") {
                    lvl.insert = InsertPosition::Top;
                } else if (val.kind == ValueKind::String && val.strVal == "bottom") {
                    lvl.insert = InsertPosition::Bottom;
                } else {
                    outError = cur.errAt(val.line, val.col, "insert must be \"top\" or \"bottom\"");
                    return false;
                }
            } else if (key == "tie_break") {
                if (val.kind == ValueKind::String && val.strVal == "lexical") {
                    lvl.tieBreak = TieBreakMode::Lexical;
                } else if (val.kind == ValueKind::String && val.strVal == "ours-first") {
                    lvl.tieBreak = TieBreakMode::OursFirst;
                } else if (val.kind == ValueKind::String && val.strVal == "theirs-first") {
                    lvl.tieBreak = TieBreakMode::TheirsFirst;
                } else {
                    outError = cur.errAt(
                        val.line, val.col,
                        "tie_break must be \"lexical\", \"ours-first\", or \"theirs-first\"");
                    return false;
                }
            } else if (key == "same_key") {
                if (val.kind == ValueKind::String && val.strVal == "merge") {
                    lvl.sameKey = SameKeyPolicy::Merge;
                } else if (val.kind == ValueKind::String && val.strVal == "conflict") {
                    lvl.sameKey = SameKeyPolicy::Conflict;
                } else {
                    outError = cur.errAt(val.line, val.col, "same_key must be \"merge\" or \"conflict\"");
                    return false;
                }
            } else if (key == "normalize") {
                if (val.kind != ValueKind::StringArray) {
                    outError = cur.errAt(val.line, val.col, "normalize must be a string array");
                    return false;
                }
                for (const auto& norm : val.arrVal) {
                    if (norm != "trim-trailing" && norm != "collapse-ws" && norm != "ignore-case") {
                        outError = cur.errAt(val.line, val.col, "unknown normalize rule '" + norm + "'");
                        return false;
                    }
                }
                lvl.normalize = val.arrVal;
            } else {
                outError = cur.errAt(keyLine, keyCol, "unknown key '" + key + "' in [[doc.level]]");
                return false;
            }
        }
    }

    if (!hasVersion) {
        outError = "1:1: missing required top-level 'version = 1'";
        return false;
    }

    for (size_t dIdx = 0; dIdx < outConfig.docs.size(); ++dIdx) {
        auto& doc = outConfig.docs[dIdx];
        if (doc.paths.empty()) {
            outError = "[[doc]] #" + std::to_string(dIdx + 1) + " is missing required 'paths'";
            return false;
        }
        if (doc.levels.empty()) {
            outError = "[[doc]] #" + std::to_string(dIdx + 1) + " must define at least one [[doc.level]]";
            return false;
        }
        std::set<std::string> seenNames;
        for (auto& lvl : doc.levels) {
            if (lvl.name.empty()) {
                outError = "[[doc.level]] in [[doc]] #" + std::to_string(dIdx + 1) + " is missing 'name'";
                return false;
            }
            if (!seenNames.insert(lvl.name).second) {
                outError = "duplicate [[doc.level]] name '" + lvl.name + "'";
                return false;
            }
            if (lvl.startPattern.elements.empty()) {
                outError = "[[doc.level]] '" + lvl.name + "' is missing required 'start' pattern";
                return false;
            }
            if (lvl.sortBy.empty()) {
                if (lvl.order == OrderMode::DateAsc || lvl.order == OrderMode::DateDesc) {
                    lvl.sortBy = "date";
                } else if (lvl.order == OrderMode::VersionAsc || lvl.order == OrderMode::VersionDesc) {
                    lvl.sortBy = "version";
                }
            }
        }
    }

    return true;
}

bool loadConfigFile(const std::string& path, Config& outConfig, std::string& outError) {
    std::string raw;
    if (!util::readFileBytes(path, raw, outError)) {
        return false;
    }
    return parseConfigString(raw, outConfig, outError);
}

std::string discoverConfigPath(const std::string& explicitPath,
                               const std::string& targetFilePath) {
    std::error_code ec;
    if (!explicitPath.empty() && explicitPath != ".docmerge.toml") {
        return explicitPath;
    }
    if (std::filesystem::exists(".docmerge.toml", ec)) {
        return ".docmerge.toml";
    }
    if (!targetFilePath.empty()) {
        std::filesystem::path dir = std::filesystem::absolute(targetFilePath, ec).parent_path();
        while (!dir.empty()) {
            std::filesystem::path candidate = dir / ".docmerge.toml";
            if (std::filesystem::exists(candidate, ec)) {
                return candidate.string();
            }
            std::filesystem::path parent = dir.parent_path();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
    }
    return explicitPath.empty() ? ".docmerge.toml" : explicitPath;
}

}  // namespace docmerge::config

