#include "../minitest.hpp"

#include "config/config.hpp"
#include "doc/doc_parse.hpp"
#include "doc/doc_write.hpp"
#include "pattern/date.hpp"
#include "pattern/pattern.hpp"
#include "pattern/version.hpp"

#include <sstream>
#include <string>

using namespace docmerge;

TEST_CASE(date_and_version_parsing) {
    pattern::DateFormat fmt;
    std::string err;
    CHECK(pattern::compileDateFormat("YYYY-MM-DD", fmt, err));

    size_t pos = 0;
    pattern::ParsedDate pd;
    CHECK(pattern::matchDateAt(fmt, "2025-03-16  Simon", pos, pd));
    CHECK_EQ(pos, 10u);
    CHECK_EQ(pd.toSortableInt(), 20250316);

    // Invalid dates must fail
    pos = 0;
    CHECK(!pattern::matchDateAt(fmt, "2025-13-01", pos, pd));
    pos = 0;
    CHECK(!pattern::matchDateAt(fmt, "2025-02-29", pos, pd));  // 2025 is not a leap year
    pos = 0;
    CHECK(pattern::matchDateAt(fmt, "2024-02-29", pos, pd));   // 2024 is a leap year

    // Version comparison: dotted numbers and pre-release before final release
    CHECK(pattern::compareVersionStrings("1.2.0", "1.10.0") < 0);
    CHECK(pattern::compareVersionStrings("2.0.0-rc1", "2.0.0") < 0);
    CHECK(pattern::compareVersionStrings("2.0.0-alpha", "2.0.0-rc1") < 0);
    CHECK_EQ(pattern::compareVersionStrings("1.2.0", "1.2"), 0);
}

TEST_CASE(pattern_compiler_and_matcher) {
    pattern::CompiledPattern pat;
    std::string err;
    CHECK(pattern::compilePattern("date(YYYY-MM-DD) as date ws any as who", pat, err));

    const auto mr = pattern::matchLine(pat, "2025-03-16  Simon Sobisch <simon@gnu.org>");
    CHECK(mr.matched);
    CHECK_EQ(mr.captures.at("date"), "2025-03-16");
    CHECK_EQ(mr.captures.at("who"), "Simon Sobisch <simon@gnu.org>");

    const auto badMr = pattern::matchLine(pat, "\t* configure.ac: fix");
    CHECK(!badMr.matched);

    // Test spaces(N), until("x"), number
    pattern::CompiledPattern pat2;
    CHECK(pattern::compilePattern("spaces(2) \"[\" number as id \"] \" until(\":\") as scope \": \" any as desc", pat2, err));
    const auto mr2 = pattern::matchLine(pat2, "  [42] parser: handle blank lines");
    CHECK(mr2.matched);
    CHECK_EQ(mr2.captures.at("id"), "42");
    CHECK_EQ(mr2.captures.at("scope"), "parser");
    CHECK_EQ(mr2.captures.at("desc"), "handle blank lines");
}

TEST_CASE(toml_config_good_and_bad) {
    const std::string validToml = R"(
version = 1

[defaults]
on_conflict = "markers"
conflict_style = "diff3"

[[doc]]
paths = ["ChangeLog", "**/ChangeLog"]

  [[doc.level]]
  name  = "entry"
  start = 'date(YYYY-MM-DD) as date ws any as who'
  key   = "header"
  order = "date-desc"

  [[doc.level]]
  name   = "item"
  start  = 'tab "* "'
  key    = "text"
  insert = "bottom"
)";

    config::Config cfg;
    std::string err;
    CHECK(config::parseConfigString(validToml, cfg, err));
    CHECK_EQ(cfg.version, 1);
    CHECK_EQ(cfg.docs.size(), 1u);
    CHECK_EQ(cfg.docs[0].levels.size(), 2u);
    CHECK_EQ(cfg.docs[0].levels[0].sortBy, "date");
    CHECK(cfg.findDocForPath("src/ChangeLog") != nullptr);

    // Bad TOML reports line:col
    config::Config badCfg;
    CHECK(!config::parseConfigString("version = 2\n", badCfg, err));
    CHECK(err.find("1:") != std::string::npos);

    CHECK(!config::parseConfigString("version = 1\n[[doc]]\npaths = [\"NEWS\"]\n[[doc.level]]\nname = \"a\"\nstart = 'unknown_token'\n", badCfg, err));
    CHECK(err.find("6:") != std::string::npos);
}

TEST_CASE(block_tree_roundtrip_all_formats) {
    config::Config cfg;
    std::string err;
    CHECK(config::loadConfigFile( DOCMERGE_SOURCE_DIR "/examples/docmerge.toml", cfg, err));

    // 1. GNU ChangeLog sample with tabs, blank lines, and multi-line continuations
    const std::string sampleChangeLog =
        "2025-03-16  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: support setting of PANEL_LIBS\n"
        "\tand extra continuation line\n"
        "\t* ABOUT-NLS, INSTALL: replaced by current versions\n"
        "\n"
        "2025-02-22  Simon Sobisch <simonsobisch@gnu.org>\n"
        "\n"
        "\t* configure.ac: fix output for --with-curses\n";

    const auto* clRule = cfg.findDocForPath("ChangeLog");
    CHECK(clRule != nullptr);
    const doc::Document parsedCl = doc::parseDocument(sampleChangeLog, *clRule);
    CHECK_EQ(parsedCl.root.children.size(), 2u);
    CHECK_EQ(parsedCl.root.children[0].children.size(), 2u);
    CHECK_EQ(doc::writeDocument(parsedCl), sampleChangeLog);

    std::ostringstream dumpStream;
    doc::dumpDocumentTree(parsedCl, dumpStream);
    CHECK(dumpStream.str().find("entry [") != std::string::npos);
    CHECK(dumpStream.str().find("item [") != std::string::npos);

    // 2. Keep a Changelog with preamble, CRLF, and UTF-8 BOM
    const std::string sampleKeep =
        "\xEF\xBB\xBF# Changelog\r\n"
        "All notable changes to this project will be documented here.\r\n"
        "\r\n"
        "## [1.1.0] - 2025-03-10\r\n"
        "### Added\r\n"
        "- Initial tree parser\r\n"
        "- TOML configuration loader\r\n";

    const auto* keepRule = cfg.findDocForPath("CHANGELOG.md");
    CHECK(keepRule != nullptr);
    const doc::Document parsedKeep = doc::parseDocument(sampleKeep, *keepRule);
    CHECK_EQ(doc::writeDocument(parsedKeep), sampleKeep);
}

int main() {
    return minitest::runAllTests();
}
