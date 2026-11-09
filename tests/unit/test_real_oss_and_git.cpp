#include "../minitest.hpp"

#include "cli/args.hpp"
#include "cli/commands.hpp"
#include "config/config.hpp"
#include "doc/doc_parse.hpp"
#include "doc/doc_write.hpp"
#include "merge/tree_merge.hpp"
#include "util/fileio.hpp"
#include "verify/invariants.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace docmerge;

namespace {

config::Config loadRepoExampleConfig() {
    config::Config cfg;
    std::string err;
    CHECK(config::loadConfigFile(DOCMERGE_SOURCE_DIR "/examples/docmerge.toml", cfg, err));
    return cfg;
}

}  // namespace

TEST_CASE(real_gnucobol_and_gcc_files_roundtrip_and_merge) {
    const config::Config cfg = loadRepoExampleConfig();
    const auto* clRule = cfg.findDocForPath("ChangeLog");
    const auto* newsRule = cfg.findDocForPath("NEWS");
    CHECK(clRule != nullptr);
    CHECK(newsRule != nullptr);

    // 1. Test real GnuCOBOL files if present on machine
    const std::vector<std::string> gnucobolChangeLogs = {
        "/home/utam1/gnucobol/ChangeLog",
        "/home/utam1/gnucobol/cobc/ChangeLog",
        "/home/utam1/gnucobol/libcob/ChangeLog",
        "/home/utam1/gnucobol/tests/ChangeLog"};

    for (const auto& path : gnucobolChangeLogs) {
        if (std::filesystem::exists(path)) {
            std::string raw;
            std::string err;
            CHECK(util::readFileBytes(path, raw, err));
            const doc::Document parsed = doc::parseDocument(raw, *clRule);
            CHECK(!parsed.root.children.empty());
            CHECK_EQ(doc::writeDocument(parsed), raw);
        }
    }

    if (std::filesystem::exists("/home/utam1/gnucobol/NEWS")) {
        std::string rawNews;
        std::string err;
        CHECK(util::readFileBytes("/home/utam1/gnucobol/NEWS", rawNews, err));
        const doc::Document parsedNews = doc::parseDocument(rawNews, *newsRule);
        CHECK(!parsedNews.root.children.empty());
        CHECK_EQ(doc::writeDocument(parsedNews), rawNews);
    }

    // 2. Test real GCC ChangeLog files (up to 1.3 MB) + duplicate same-day same-author commits
    const std::vector<std::string> gccChangeLogs = {
        "/home/utam1/gcc/ChangeLog",
        "/home/utam1/gcc/gcc/ChangeLog"};

    for (const auto& path : gccChangeLogs) {
        if (std::filesystem::exists(path)) {
            std::string raw;
            std::string err;
            CHECK(util::readFileBytes(path, raw, err));
            const doc::Document parsed = doc::parseDocument(raw, *clRule);
            CHECK(!parsed.root.children.empty());
            CHECK_EQ(doc::writeDocument(parsed), raw);

            // Simulate two concurrent GCC PRs modifying the top of the 1.3 MB ChangeLog:
            // PR 1 (ours): Jeff Law adds another commit on 2026-11-01 (duplicate header!)
            // PR 2 (theirs): Alice adds a commit on 2026-11-02
            const std::string prOursEntry =
                "2026-11-01  Jeff Law  <jeffreyalaw@gmail.com>\n"
                "\n"
                "\tPR target/999999\n"
                "\t* config/riscv/riscv.cc: Handle new edge case in PR merge.\n\n";

            const std::string prTheirsEntry =
                "2026-11-02  Alice Maintainer  <alice@gcc.gnu.org>\n"
                "\n"
                "\tPR middle-end/888888\n"
                "\t* tree-ssa.cc: Optimize phi node folding.\n\n";

            const doc::Document oursDoc = doc::parseDocument(prOursEntry + raw, *clRule);
            const doc::Document theirsDoc = doc::parseDocument(prTheirsEntry + raw, *clRule);

            merge::ConflictMarkerConfig markerCfg;
            const auto merged =
                merge::mergeDocumentTrees(parsed, oursDoc, theirsDoc, *clRule, markerCfg);
            CHECK(!merged.hasConflicts);
            const std::string mergedBytes = doc::writeDocument(merged.document, markerCfg);
            // Both new PR entries must be present and 2026-11-02 must be at the very top
            CHECK_EQ(mergedBytes.find(prTheirsEntry), 0u);
            CHECK(mergedBytes.find("PR target/999999") != std::string::npos);
            const auto ver = verify::verifyMergedOutput(
                parsed, oursDoc, theirsDoc, merged.document, mergedBytes, false, *clRule, markerCfg);
            CHECK(ver.ok);
        }
    }
}

TEST_CASE(cpython_restructuredtext_whatsnew_and_coreutils_news) {
    const config::Config cfg = loadRepoExampleConfig();
    const auto* rstRule = cfg.findDocForPath("Doc/whatsnew/3.13.rst");
    const auto* coreutilsRule = cfg.findDocForPath("coreutils/NEWS");
    CHECK(rstRule != nullptr);
    CHECK(coreutilsRule != nullptr);

    // 1. CPython What's New .rst: concurrent PRs adding module subsections & items
    const std::string baseRst =
        "****************************\n"
        "  What's New In Python 3.13\n"
        "****************************\n"
        "\n"
        "Other Language Changes\n"
        "======================\n"
        "\n"
        "* First language change in base.\n"
        "\n"
        "Improved Modules\n"
        "================\n"
        "\n"
        "argparse\n"
        "--------\n"
        "\n"
        "* Existing argparse improvement.\n"
        "\n"
        "ctypes\n"
        "------\n"
        "\n"
        "* Existing ctypes improvement.\n";

    // Ours adds a new module `csv` (between argparse and ctypes) and an item under `argparse`
    const std::string oursRst =
        "****************************\n"
        "  What's New In Python 3.13\n"
        "****************************\n"
        "\n"
        "Other Language Changes\n"
        "======================\n"
        "\n"
        "* First language change in base.\n"
        "\n"
        "Improved Modules\n"
        "================\n"
        "\n"
        "argparse\n"
        "--------\n"
        "\n"
        "* Existing argparse improvement.\n"
        "* Added deprecation support to subparsers. (Contributed by Alice in :gh:`10001`.)\n"
        "\n"
        "csv\n"
        "---\n"
        "\n"
        "* Improved quoting dialect handling. (Contributed by Alice in :gh:`10002`.)\n"
        "\n"
        "ctypes\n"
        "------\n"
        "\n"
        "* Existing ctypes improvement.\n";

    // Theirs adds a new module `asyncio` (also between argparse and ctypes) and another item under `argparse`
    const std::string theirsRst =
        "****************************\n"
        "  What's New In Python 3.13\n"
        "****************************\n"
        "\n"
        "Other Language Changes\n"
        "======================\n"
        "\n"
        "* First language change in base.\n"
        "\n"
        "Improved Modules\n"
        "================\n"
        "\n"
        "argparse\n"
        "--------\n"
        "\n"
        "* Existing argparse improvement.\n"
        "* Added colorized help output. (Contributed by Bob in :gh:`10003`.)\n"
        "\n"
        "asyncio\n"
        "-------\n"
        "\n"
        "* Improved TaskGroup cancellation speed. (Contributed by Bob in :gh:`10004`.)\n"
        "\n"
        "ctypes\n"
        "------\n"
        "\n"
        "* Existing ctypes improvement.\n";

    const doc::Document bRstDoc = doc::parseDocument(baseRst, *rstRule);
    const doc::Document oRstDoc = doc::parseDocument(oursRst, *rstRule);
    const doc::Document tRstDoc = doc::parseDocument(theirsRst, *rstRule);

    CHECK_EQ(doc::writeDocument(bRstDoc), baseRst);

    merge::ConflictMarkerConfig markerCfg;
    const auto rstMerged =
        merge::mergeDocumentTrees(bRstDoc, oRstDoc, tRstDoc, *rstRule, markerCfg);
    CHECK(!rstMerged.hasConflicts);
    const std::string rstMergedBytes = doc::writeDocument(rstMerged.document, markerCfg);

    // Verify alphabetical module ordering: argparse -> asyncio -> csv -> ctypes
    const size_t posArgparse = rstMergedBytes.find("argparse\n--------");
    const size_t posAsyncio = rstMergedBytes.find("asyncio\n-------");
    const size_t posCsv = rstMergedBytes.find("csv\n---");
    const size_t posCtypes = rstMergedBytes.find("ctypes\n------");
    CHECK(posArgparse != std::string::npos);
    CHECK(posAsyncio != std::string::npos);
    CHECK(posCsv != std::string::npos);
    CHECK(posCtypes != std::string::npos);
    CHECK(posArgparse < posAsyncio);
    CHECK(posAsyncio < posCsv);
    CHECK(posCsv < posCtypes);

    // Verify both items under `argparse` were kept
    CHECK(rstMergedBytes.find(":gh:`10001`") != std::string::npos);
    CHECK(rstMergedBytes.find(":gh:`10003`") != std::string::npos);

    // 2. GNU Coreutils NEWS: 2-space indented multi-line paragraph items after blank lines
    const std::string baseCoreutils =
        "GNU coreutils NEWS                                    -*- outline -*-\n"
        "\n"
        "* Noteworthy changes in release ?.? (?\?\?\?-\?\?-\?\?) [?]\n"
        "\n"
        "** Bug fixes\n"
        "\n"
        "  'basenc --base32hex' no longer decodes BASE32 only characters \"WXYZ\".\n"
        "  [bug introduced with the basenc program in coreutils-8.31]\n";

    const std::string oursCoreutils =
        baseCoreutils +
        "\n"
        "  'chcon' avoids a race between getting and setting security contexts.\n"
        "  [bug introduced in coreutils-6.9.90]\n";

    const std::string theirsCoreutils =
        baseCoreutils +
        "\n"
        "  'cksum' now detects GNU and BSD reversed formats for each checksum file.\n"
        "  [bug introduced in coreutils-8.14]\n";

    const doc::Document bCuDoc = doc::parseDocument(baseCoreutils, *coreutilsRule);
    const doc::Document oCuDoc = doc::parseDocument(oursCoreutils, *coreutilsRule);
    const doc::Document tCuDoc = doc::parseDocument(theirsCoreutils, *coreutilsRule);

    const auto cuMerged =
        merge::mergeDocumentTrees(bCuDoc, oCuDoc, tCuDoc, *coreutilsRule, markerCfg);
    CHECK(!cuMerged.hasConflicts);
    const std::string cuMergedBytes = doc::writeDocument(cuMerged.document, markerCfg);
    CHECK(cuMergedBytes.find("'basenc --base32hex'") != std::string::npos);
    CHECK(cuMergedBytes.find("'chcon' avoids a race") != std::string::npos);
    CHECK(cuMergedBytes.find("'cksum' now detects") != std::string::npos);
}

TEST_CASE(actual_git_pr_environment_end_to_end) {
    const std::filesystem::path repoDir =
        std::filesystem::temp_directory_path() / "docmerge_real_git_pr_test";
    std::filesystem::remove_all(repoDir);
    std::filesystem::create_directories(repoDir);

    const std::string docmergeBin = DOCMERGE_BINARY_PATH;
    std::string ioErr;

    // Copy .docmerge.toml and create .gitattributes
    std::string cfgBytes;
    CHECK(util::readFileBytes(DOCMERGE_SOURCE_DIR "/examples/docmerge.toml", cfgBytes, ioErr));
    CHECK(util::writeFileAtomic((repoDir / ".docmerge.toml").string(), cfgBytes, ioErr));

    const std::string gitattributes =
        "NEWS merge=docmerge\n"
        "ChangeLog merge=docmerge\n"
        "*.rst merge=docmerge\n";
    CHECK(util::writeFileAtomic((repoDir / ".gitattributes").string(), gitattributes, ioErr));

    const std::string baseChangeLog =
        "2025-11-11  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: prefer \"inline\" over \"__inline\"\n";
    CHECK(util::writeFileAtomic((repoDir / "ChangeLog").string(), baseChangeLog, ioErr));

    const std::string baseNews =
        "* New GnuCOBOL features\n"
        "\n"
        "** support the COLLATING SEQUENCE clause on indexed files\n";
    CHECK(util::writeFileAtomic((repoDir / "NEWS").string(), baseNews, ioErr));

    auto runInRepo = [&](const std::string& cmd) -> int {
        const std::string fullCmd =
            "cd \"" + repoDir.string() + "\" && " + cmd + " >/dev/null 2>&1";
        return std::system(fullCmd.c_str());
    };

    // Initialize repo and create base commit on `main`
    CHECK_EQ(runInRepo("git init -b main || (git init && git checkout -b main)"), 0);
    CHECK_EQ(runInRepo("git config user.name \"Test Bot\""), 0);
    CHECK_EQ(runInRepo("git config user.email \"bot@example.org\""), 0);
    CHECK_EQ(runInRepo("git add .docmerge.toml .gitattributes ChangeLog NEWS && git commit -m \"Initial base docs\""), 0);

    // Create PR branch `pr-branch` that adds an entry to ChangeLog and NEWS
    CHECK_EQ(runInRepo("git checkout -b pr-branch"), 0);
    const std::string prChangeLog =
        "2025-11-19  Oguzcan Kirmemis <oguzcan@example.org>\n"
        "\n"
        "\t* configure.ac: pass HAVE_SIGNAL_H to the tests\n"
        "\n" +
        baseChangeLog;
    const std::string prNews =
        baseNews +
        "\n"
        "** new runtime configuration COB_SIGNAL_REGIME\n";
    CHECK(util::writeFileAtomic((repoDir / "ChangeLog").string(), prChangeLog, ioErr));
    CHECK(util::writeFileAtomic((repoDir / "NEWS").string(), prNews, ioErr));
    CHECK_EQ(runInRepo("git commit -am \"PR: add COB_SIGNAL_REGIME docs\""), 0);

    // Meanwhile on `main`, another PR is merged adding a newer entry at the exact same lines!
    CHECK_EQ(runInRepo("git checkout main"), 0);
    const std::string mainChangeLog =
        "2025-11-20  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: fix linking order for PANEL_LIBS\n"
        "\n" +
        baseChangeLog;
    const std::string mainNews =
        baseNews +
        "\n"
        "** new runtime configuration COB_HIDE_CURSOR\n";
    CHECK(util::writeFileAtomic((repoDir / "ChangeLog").string(), mainChangeLog, ioErr));
    CHECK(util::writeFileAtomic((repoDir / "NEWS").string(), mainNews, ioErr));
    CHECK_EQ(runInRepo("git commit -am \"main: add PANEL_LIBS and COB_HIDE_CURSOR docs\""), 0);

    // Configure our built `docmerge` binary as the Git merge driver in `.git/config`
    const std::string driverCmd =
        "\"" + docmergeBin + "\" merge %O %A %B %L %P";
    CHECK_EQ(runInRepo("git config merge.docmerge.name \"documentation-aware merge\""), 0);
    CHECK_EQ(runInRepo("git config merge.docmerge.driver '" + driverCmd + "'"), 0);

    // Perform a real `git merge pr-branch`!
    CHECK_EQ(runInRepo("git merge --no-edit pr-branch"), 0);

    // Verify both ChangeLog and NEWS merged cleanly with all entries present!
    std::string finalChangeLog;
    std::string finalNews;
    CHECK(util::readFileBytes((repoDir / "ChangeLog").string(), finalChangeLog, ioErr));
    CHECK(util::readFileBytes((repoDir / "NEWS").string(), finalNews, ioErr));

    CHECK(finalChangeLog.find("<<<<<<<") == std::string::npos);
    const size_t posNov20 = finalChangeLog.find("2025-11-20  Simon Sobisch");
    const size_t posNov19 = finalChangeLog.find("2025-11-19  Oguzcan Kirmemis");
    const size_t posNov11 = finalChangeLog.find("2025-11-11  Simon Sobisch");
    CHECK(posNov20 != std::string::npos);
    CHECK(posNov19 != std::string::npos);
    CHECK(posNov11 != std::string::npos);
    CHECK(posNov20 < posNov19);
    CHECK(posNov19 < posNov11);

    CHECK(finalNews.find("<<<<<<<") == std::string::npos);
    CHECK(finalNews.find("COB_HIDE_CURSOR") != std::string::npos);
    CHECK(finalNews.find("COB_SIGNAL_REGIME") != std::string::npos);

    std::filesystem::remove_all(repoDir);
}

int main() {
    return minitest::runAllTests();
}
