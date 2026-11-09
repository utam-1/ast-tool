#include "../minitest.hpp"

#include "cli/args.hpp"
#include "cli/commands.hpp"
#include "config/config.hpp"
#include "doc/doc_parse.hpp"
#include "doc/doc_write.hpp"
#include "merge/tree_merge.hpp"
#include "util/fileio.hpp"
#include "verify/invariants.hpp"

#include <filesystem>
#include <random>
#include <sstream>
#include <string>

using namespace docmerge;

namespace {

config::Config loadExampleConfig() {
    config::Config cfg;
    std::string err;
    CHECK(config::loadConfigFile(DOCMERGE_SOURCE_DIR "/examples/docmerge.toml", cfg, err));
    return cfg;
}

}  // namespace

TEST_CASE(example_10_1_changelog_both_added_dated_entry_at_top) {
    const config::Config cfg = loadExampleConfig();
    const auto* rule = cfg.findDocForPath("ChangeLog");
    CHECK(rule != nullptr);

    const std::string baseText =
        "2025-03-16  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: support setting of PANEL_LIBS\n"
        "\n"
        "2025-02-22  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: fix output for --with-curses\n";

    const std::string oursText =
        "2025-03-20  Alice <alice@example.org>\n"
        "\n"
        "\t* foo.c: fix X\n"
        "\n"
        + baseText;

    const std::string theirsText =
        "2025-03-18  Bob <bob@example.org>\n"
        "\n"
        "\t* bar.c: fix Y\n"
        "\n"
        + baseText;

    const std::string expectedText =
        "2025-03-20  Alice <alice@example.org>\n"
        "\n"
        "\t* foo.c: fix X\n"
        "\n"
        "2025-03-18  Bob <bob@example.org>\n"
        "\n"
        "\t* bar.c: fix Y\n"
        "\n"
        + baseText;

    const doc::Document bDoc = doc::parseDocument(baseText, *rule);
    const doc::Document oDoc = doc::parseDocument(oursText, *rule);
    const doc::Document tDoc = doc::parseDocument(theirsText, *rule);

    merge::ConflictMarkerConfig markerCfg;
    const auto resOT = merge::mergeDocumentTrees(bDoc, oDoc, tDoc, *rule, markerCfg);
    CHECK(!resOT.hasConflicts);
    const std::string outOT = doc::writeDocument(resOT.document, markerCfg);
    CHECK_EQ(outOT, expectedText);

    // Symmetry check: merge(base, theirs, ours) == merge(base, ours, theirs)
    const auto resTO = merge::mergeDocumentTrees(bDoc, tDoc, oDoc, *rule, markerCfg);
    CHECK(!resTO.hasConflicts);
    const std::string outTO = doc::writeDocument(resTO.document, markerCfg);
    CHECK_EQ(outTO, expectedText);

    // Post-merge verification passes
    const auto ver = verify::verifyMergedOutput(
        bDoc, oDoc, tDoc, resOT.document, outOT, resOT.hasConflicts, *rule, markerCfg);
    CHECK(ver.ok);
}

TEST_CASE(example_10_2_changelog_both_added_item_under_same_header) {
    const config::Config cfg = loadExampleConfig();
    const auto* rule = cfg.findDocForPath("ChangeLog");
    CHECK(rule != nullptr);

    const std::string baseText =
        "2025-03-16  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: base item\n";

    const std::string oursText =
        "2025-03-16  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: base item\n"
        "\t* zebra.c: added by ours\n";

    const std::string theirsText =
        "2025-03-16  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: base item\n"
        "\t* alpha.c: added by theirs\n";

    const std::string expectedText =
        "2025-03-16  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: base item\n"
        "\t* alpha.c: added by theirs\n"
        "\t* zebra.c: added by ours\n";

    const doc::Document bDoc = doc::parseDocument(baseText, *rule);
    const doc::Document oDoc = doc::parseDocument(oursText, *rule);
    const doc::Document tDoc = doc::parseDocument(theirsText, *rule);

    merge::ConflictMarkerConfig markerCfg;
    const auto resOT = merge::mergeDocumentTrees(bDoc, oDoc, tDoc, *rule, markerCfg);
    CHECK(!resOT.hasConflicts);
    CHECK_EQ(doc::writeDocument(resOT.document, markerCfg), expectedText);

    // Symmetric when ours and theirs are swapped (e.g. git rebase)
    const auto resTO = merge::mergeDocumentTrees(bDoc, tDoc, oDoc, *rule, markerCfg);
    CHECK(!resTO.hasConflicts);
    CHECK_EQ(doc::writeDocument(resTO.document, markerCfg), expectedText);
}

TEST_CASE(example_10_3_and_10_4_news_entries_and_body_conflict) {
    const config::Config cfg = loadExampleConfig();
    const auto* rule = cfg.findDocForPath("NEWS");
    CHECK(rule != nullptr);

    // 10.3: Both added an entry at the end of the same section
    const std::string baseNews =
        "* New GnuCOBOL features\n"
        "\n"
        "** support the COLLATING SEQUENCE clause\n"
        "   Initial support for alphanumeric collations.\n";

    const std::string oursNews =
        baseNews +
        "\n"
        "** new feature Beta\n"
        "   Added beta support.\n";

    const std::string theirsNews =
        baseNews +
        "\n"
        "** new feature Alpha\n"
        "   Added alpha support.\n";

    const doc::Document bDoc = doc::parseDocument(baseNews, *rule);
    const doc::Document oDoc = doc::parseDocument(oursNews, *rule);
    const doc::Document tDoc = doc::parseDocument(theirsNews, *rule);

    merge::ConflictMarkerConfig markerCfg;
    const auto res10_3 = merge::mergeDocumentTrees(bDoc, oDoc, tDoc, *rule, markerCfg);
    CHECK(!res10_3.hasConflicts);
    const std::string merged10_3 = doc::writeDocument(res10_3.document, markerCfg);
    CHECK(merged10_3.find("** new feature Alpha") != std::string::npos);
    CHECK(merged10_3.find("** new feature Beta") != std::string::npos);

    // 10.4: Both edited the body of the same entry differently -> localized conflict
    const std::string oursBodyEdit =
        "* New GnuCOBOL features\n"
        "\n"
        "** support the COLLATING SEQUENCE clause\n"
        "   Ours rewrote this sentence completely.\n";

    const std::string theirsBodyEdit =
        "* New GnuCOBOL features\n"
        "\n"
        "** support the COLLATING SEQUENCE clause\n"
        "   Theirs rewrote this sentence in another way.\n";

    const doc::Document oEditDoc = doc::parseDocument(oursBodyEdit, *rule);
    const doc::Document tEditDoc = doc::parseDocument(theirsBodyEdit, *rule);

    const auto res10_4 = merge::mergeDocumentTrees(bDoc, oEditDoc, tEditDoc, *rule, markerCfg);
    CHECK(res10_4.hasConflicts);
    CHECK_EQ(res10_4.conflictCount, 1u);
    const std::string merged10_4 = doc::writeDocument(res10_4.document, markerCfg);
    CHECK(merged10_4.find("<<<<<<< ours") != std::string::npos);
    CHECK(merged10_4.find("||||||| base") != std::string::npos);
    CHECK(merged10_4.find(">>>>>>> theirs") != std::string::npos);
}

TEST_CASE(cli_resolve_check_dump_init_and_install) {
    const std::filesystem::path tmpDir =
        std::filesystem::temp_directory_path() / "docmerge_test_cli_full";
    std::filesystem::create_directories(tmpDir);

    std::string ioErr;
    const std::string cfgPath = (tmpDir / ".docmerge.toml").string();
    std::string exampleCfgBytes;
    CHECK(util::readFileBytes(DOCMERGE_SOURCE_DIR "/examples/docmerge.toml", exampleCfgBytes, ioErr));
    CHECK(util::writeFileAtomic(cfgPath, exampleCfgBytes, ioErr));

    // 1. Test `docmerge resolve` on a conflicted CHANGELOG.md with diff3 markers
    const std::string conflictedChangelog =
        "<<<<<<< ours\n"
        "## [1.2.0] - 2025-03-20\n"
        "### Added\n"
        "- Feature from ours\n"
        "\n"
        "||||||| base\n"
        "=======\n"
        "## [1.1.5] - 2025-03-18\n"
        "### Fixed\n"
        "- Bugfix from theirs\n"
        "\n"
        ">>>>>>> theirs\n"
        "## [1.1.0] - 2025-03-01\n"
        "### Added\n"
        "- Initial release\n";

    const std::string clPath = (tmpDir / "CHANGELOG.md").string();
    CHECK(util::writeFileAtomic(clPath, conflictedChangelog, ioErr));

    {
        cli::ParsedArgs args;
        args.command = "resolve";
        args.configPath = cfgPath;
        args.positional = {clPath};
        std::ostringstream out;
        std::ostringstream err;
        CHECK_EQ(cli::runCommand(args, out, err), 0);

        std::string resolved;
        CHECK(util::readFileBytes(clPath, resolved, ioErr));
        CHECK(resolved.find("<<<<<<<") == std::string::npos);
        CHECK(resolved.find("## [1.2.0] - 2025-03-20") != std::string::npos);
        CHECK(resolved.find("## [1.1.5] - 2025-03-18") != std::string::npos);
        CHECK(resolved.find("## [1.1.0] - 2025-03-01") != std::string::npos);
    }

    // 2. Test `docmerge check` and `docmerge dump` and `docmerge init`
    {
        cli::ParsedArgs checkArgs;
        checkArgs.command = "check";
        checkArgs.configPath = cfgPath;
        checkArgs.positional = {clPath};
        std::ostringstream out;
        std::ostringstream err;
        CHECK_EQ(cli::runCommand(checkArgs, out, err), 0);
        CHECK(out.str().find("release=3") != std::string::npos);
    }
    {
        cli::ParsedArgs dumpArgs;
        dumpArgs.command = "dump";
        dumpArgs.configPath = cfgPath;
        dumpArgs.positional = {clPath};
        std::ostringstream out;
        std::ostringstream err;
        CHECK_EQ(cli::runCommand(dumpArgs, out, err), 0);
        CHECK(out.str().find("(root)") != std::string::npos);
    }
    {
        cli::ParsedArgs initArgs;
        initArgs.command = "init";
        initArgs.positional = {clPath};
        std::ostringstream out;
        std::ostringstream err;
        CHECK_EQ(cli::runCommand(initArgs, out, err), 0);
        CHECK(out.str().find("version = 1") != std::string::npos);
        CHECK(out.str().find("[[doc.level]]") != std::string::npos);
    }
    {
        cli::ParsedArgs installArgs;
        installArgs.command = "install";
        installArgs.configPath = cfgPath;
        installArgs.dryRun = true;
        std::ostringstream out;
        std::ostringstream err;
        CHECK_EQ(cli::runCommand(installArgs, out, err), 0);
        CHECK(out.str().find("merge.docmerge.driver") != std::string::npos);
    }

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE(property_and_fuzz_mutation_stability) {
    const config::Config cfg = loadExampleConfig();
    const auto* rule = cfg.findDocForPath("ChangeLog");
    CHECK(rule != nullptr);

    const std::string seedDoc =
        "2025-03-16  Simon <simon@gnu.org>\n\n"
        "\t* a.c: change 1\n"
        "\t* b.c: change 2\n\n"
        "2025-02-10  Alice <alice@gnu.org>\n\n"
        "\t* c.c: change 3\n";

    // Property: merge(B, O, O) == O and merge(B, B, T) == T
    const std::string modDoc =
        "2025-03-22  Bob <bob@gnu.org>\n\n"
        "\t* d.c: change 4\n\n" +
        seedDoc;

    const doc::Document bDoc = doc::parseDocument(seedDoc, *rule);
    const doc::Document oDoc = doc::parseDocument(modDoc, *rule);
    merge::ConflictMarkerConfig markerCfg;

    const auto resBOO = merge::mergeDocumentTrees(bDoc, oDoc, oDoc, *rule, markerCfg);
    CHECK_EQ(doc::writeDocument(resBOO.document, markerCfg), modDoc);

    const auto resBBT = merge::mergeDocumentTrees(bDoc, bDoc, oDoc, *rule, markerCfg);
    CHECK_EQ(doc::writeDocument(resBBT.document, markerCfg), modDoc);

    // Fuzz: 50 random byte mutations must never crash or violate round-trip parse
    std::mt19937 rng(42);
    for (int iter = 0; iter < 50; ++iter) {
        std::string mutated = seedDoc;
        const size_t pos = rng() % mutated.size();
        mutated[pos] = static_cast<char>(32 + (rng() % 90));

        const doc::Document mDoc = doc::parseDocument(mutated, *rule);
        CHECK_EQ(doc::writeDocument(mDoc), mutated);

        const auto mRes = merge::mergeDocumentTrees(bDoc, oDoc, mDoc, *rule, markerCfg);
        const std::string mBytes = doc::writeDocument(mRes.document, markerCfg);
        CHECK(!mBytes.empty());
    }
}

int main() {
    return minitest::runAllTests();
}
