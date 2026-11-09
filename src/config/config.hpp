#pragma once

#include "merge/conflict.hpp"
#include "pattern/pattern.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace docmerge::config {

enum class OrderMode {
    None,
    DateAsc,
    DateDesc,
    VersionAsc,
    VersionDesc,
    AlphaAsc,
    AlphaDesc
};

enum class InsertPosition {
    Top,
    Bottom
};

enum class TieBreakMode {
    Lexical,
    OursFirst,
    TheirsFirst
};

enum class SameKeyPolicy {
    Merge,
    Conflict
};

enum class OnConflictPolicy {
    Markers,
    Fail
};

struct LevelRule {
    int levelIndex = 1;  // 1..N (outermost is 1)
    std::string name;
    std::string startPatternRaw;
    pattern::CompiledPattern startPattern;
    std::string keySpec = "header";  // "header", "text", or "capture:NAME"
    OrderMode order = OrderMode::None;
    std::string sortBy;              // Capture name or empty (implies key)
    InsertPosition insert = InsertPosition::Bottom;
    TieBreakMode tieBreak = TieBreakMode::Lexical;
    SameKeyPolicy sameKey = SameKeyPolicy::Merge;
    std::vector<std::string> normalize = {"trim-trailing"};
};

struct DocRule {
    std::vector<std::string> paths;
    OnConflictPolicy onConflict = OnConflictPolicy::Markers;
    merge::ConflictStyle conflictStyle = merge::ConflictStyle::Diff3;
    std::vector<LevelRule> levels;
};

struct Config {
    int version = 1;
    OnConflictPolicy defaultOnConflict = OnConflictPolicy::Markers;
    merge::ConflictStyle defaultConflictStyle = merge::ConflictStyle::Diff3;
    std::vector<DocRule> docs;

    // Finds the first `DocRule` whose `paths` globs match `repoRelativePath`.
    const DocRule* findDocForPath(std::string_view repoRelativePath) const;
};

// Parses and validates a `.docmerge.toml` text string.
// Errors include `line:column: message`.
bool parseConfigString(std::string_view tomlText, Config& outConfig, std::string& outError);

// Loads and validates a `.docmerge.toml` file from disk.
bool loadConfigFile(const std::string& path, Config& outConfig, std::string& outError);

// Finds `.docmerge.toml` by checking `explicitPath` first, or walking upward from `targetFilePath`
// and the current working directory.
std::string discoverConfigPath(const std::string& explicitPath,
                               const std::string& targetFilePath);

}  // namespace docmerge::config
