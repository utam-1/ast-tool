#include "../minitest.hpp"

#include "cli/args.hpp"
#include "cli/commands.hpp"
#include "doc/line.hpp"
#include "merge/conflict.hpp"
#include "merge/diff.hpp"
#include "merge/diff3.hpp"
#include "util/fileio.hpp"
#include "util/glob.hpp"
#include "util/hash.hpp"
#include "util/strings.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace docmerge;

TEST_CASE(line_split_and_join_roundtrip) {
    // 1. Standard LF file
    const std::string lfInput = "first line\nsecond line\n\nfourth line\n";
    const auto lfSplit = doc::splitLines(lfInput);
    CHECK(!lfSplit.hasBom);
    CHECK_EQ(lfSplit.eolStyle, "\n");
    CHECK_EQ(lfSplit.lines.size(), 4u);
    CHECK(lfSplit.lines[2].isBlank());
    CHECK_EQ(doc::joinLines(lfSplit.lines, lfSplit.hasBom), lfInput);

    // 2. CRLF + no trailing newline + tabs
    const std::string crlfNoEof = "header\r\n\t* item 1\r\n\t* item 2";
    const auto crlfSplit = doc::splitLines(crlfNoEof);
    CHECK_EQ(crlfSplit.eolStyle, "\r\n");
    CHECK_EQ(crlfSplit.lines.size(), 3u);
    CHECK_EQ(crlfSplit.lines[2].eol, "");
    CHECK_EQ(doc::joinLines(crlfSplit.lines, crlfSplit.hasBom), crlfNoEof);

    // 3. UTF-8 BOM + mixed line endings
    const std::string bomMixed = "\xEF\xBB\xBFline1\r\nline2\nline3\r\n";
    const auto bomSplit = doc::splitLines(bomMixed);
    CHECK(bomSplit.hasBom);
    CHECK_EQ(bomSplit.eolStyle, "\r\n");
    CHECK_EQ(doc::joinLines(bomSplit.lines, bomSplit.hasBom), bomMixed);
}

TEST_CASE(strings_and_key_normalization) {
    CHECK_EQ(util::trimTrailingWhitespace("hello \t "), "hello");
    CHECK_EQ(util::collapseWhitespace("a   b\t\tc"), "a b c");
    CHECK_EQ(util::toLowerAscii("ChangeLog-1.0"), "changelog-1.0");

    const std::vector<std::string> rules = {"trim-trailing", "collapse-ws", "ignore-case"};
    CHECK_EQ(util::normalizeKey("##   Release   1.2.0  \t", rules), "## release 1.2.0");
}

TEST_CASE(glob_matching) {
    CHECK(util::globMatch("ChangeLog", "ChangeLog"));
    CHECK(util::globMatch("**/ChangeLog", "ChangeLog"));
    CHECK(util::globMatch("**/ChangeLog", "src/ChangeLog"));
    CHECK(util::globMatch("**/ChangeLog", "a/b/c/ChangeLog"));
    CHECK(!util::globMatch("**/ChangeLog", "ChangeLog.old"));
    CHECK(util::globMatch("docs/*.md", "docs/CHANGELOG.md"));
    CHECK(!util::globMatch("docs/*.md", "docs/sub/CHANGELOG.md"));
    CHECK(util::globMatch("docs/**/CHANGELOG.?", "docs/a/b/CHANGELOG.1"));
}

TEST_CASE(myers_diff_basic_and_identical) {
    const auto a = doc::splitLines("a\nb\nc\nd\n").lines;
    const auto b = doc::splitLines("a\nb\nc\nd\n").lines;
    const auto sameDiff = merge::diffLines(a, b);
    CHECK(sameDiff.hunks.empty());

    const auto c = doc::splitLines("a\nB_MOD\nc\nd\nE_ADD\n").lines;
    const auto diffRes = merge::diffLines(a, c);
    CHECK(!diffRes.tooComplex);
    CHECK_EQ(diffRes.hunks.size(), 2u);
    // Hunk 0: line 1 replaced
    CHECK_EQ(diffRes.hunks[0].aStart, 1u);
    CHECK_EQ(diffRes.hunks[0].aEnd, 2u);
    CHECK_EQ(diffRes.hunks[0].bStart, 1u);
    CHECK_EQ(diffRes.hunks[0].bEnd, 2u);
    // Hunk 1: line 4 inserted at end
    CHECK_EQ(diffRes.hunks[1].aStart, 4u);
    CHECK_EQ(diffRes.hunks[1].aEnd, 4u);
    CHECK_EQ(diffRes.hunks[1].bStart, 4u);
    CHECK_EQ(diffRes.hunks[1].bEnd, 5u);
}

TEST_CASE(diff3_clean_and_conflicting_merges) {
    const auto base = doc::splitLines("line1\nline2\nline3\nline4\n").lines;
    const auto ours = doc::splitLines("OURS1\nline2\nline3\nline4\n").lines;
    const auto theirs = doc::splitLines("line1\nline2\nline3\nTHEIRS4\n").lines;

    // 1. Disjoint edits merge cleanly
    const auto cleanRes = merge::diff3Merge(base, ours, theirs);
    CHECK(!cleanRes.hasConflicts);
    merge::ConflictMarkerConfig cfg;
    const std::string mergedText = doc::joinLines(cleanRes.toLines(cfg));
    CHECK_EQ(mergedText, "OURS1\nline2\nline3\nTHEIRS4\n");

    // 2. Identical edits on both sides merge cleanly
    const auto sameRes = merge::diff3Merge(base, ours, ours);
    CHECK(!sameRes.hasConflicts);
    CHECK_EQ(doc::joinLines(sameRes.toLines(cfg)), "OURS1\nline2\nline3\nline4\n");

    // 3. Touching hunks (line 2 edited in ours, line 3 edited in theirs) conflict
    const auto touchOurs = doc::splitLines("line1\nOURS2\nline3\nline4\n").lines;
    const auto touchTheirs = doc::splitLines("line1\nline2\nTHEIRS3\nline4\n").lines;
    const auto touchRes = merge::diff3Merge(base, touchOurs, touchTheirs);
    CHECK(touchRes.hasConflicts);
    CHECK_EQ(touchRes.conflictCount, 1u);

    const std::string expectedDiff3 =
        "line1\n"
        "<<<<<<< ours\n"
        "OURS2\n"
        "line3\n"
        "||||||| base\n"
        "line2\n"
        "line3\n"
        "=======\n"
        "line2\n"
        "THEIRS3\n"
        ">>>>>>> theirs\n"
        "line4\n";
    CHECK_EQ(doc::joinLines(touchRes.toLines(cfg)), expectedDiff3);
}

TEST_CASE(cli_merge_driver_end_to_end) {
    const std::filesystem::path tmpDir =
        std::filesystem::temp_directory_path() / "docmerge_test_m1";
    std::filesystem::create_directories(tmpDir);

    const std::string basePath = (tmpDir / "base.txt").string();
    const std::string oursPath = (tmpDir / "ours.txt").string();
    const std::string theirsPath = (tmpDir / "theirs.txt").string();

    std::string err;
    CHECK(util::writeFileAtomic(basePath, "alpha\nbeta\ngamma\n", err));
    CHECK(util::writeFileAtomic(oursPath, "ALPHA_OURS\nbeta\ngamma\n", err));
    CHECK(util::writeFileAtomic(theirsPath, "alpha\nbeta\nGAMMA_THEIRS\n", err));

    cli::ParsedArgs args;
    args.command = "merge";
    args.positional = {basePath, oursPath, theirsPath, "7", "NEWS"};

    std::ostringstream outStream;
    std::ostringstream errStream;
    const int exitCode = cli::runCommand(args, outStream, errStream);
    CHECK_EQ(exitCode, 0);

    std::string mergedContent;
    CHECK(util::readFileBytes(oursPath, mergedContent, err));
    CHECK_EQ(mergedContent, "ALPHA_OURS\nbeta\nGAMMA_THEIRS\n");

    std::filesystem::remove_all(tmpDir);
}

int main() {
    return minitest::runAllTests();
}

