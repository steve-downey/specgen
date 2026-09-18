// tests/beman/specgen/depfile.test.cpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/depfile.hpp>
#include <beman/specgen/depfile.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace beman::specgen::depfile;

TEST_CASE("depfile::escape - an ordinary path passes through untouched") {
    CHECK(escape("include/beman/expected/expected.hpp") == "include/beman/expected/expected.hpp");
    CHECK(escape("") == "");
}

TEST_CASE("depfile::escape - make's three metacharacters") {
    CHECK(escape("my header.hpp") == "my\\ header.hpp");
    CHECK(escape("c#minor.hpp") == "c\\#minor.hpp");
    CHECK(escape("$(generated).hpp") == "$$(generated).hpp");
    CHECK(escape("a b#c$d") == "a\\ b\\#c$$d");
}

TEST_CASE("depfile::escape - a backslash is left alone") {
    // Not an escape on the platforms that use it as a separator, so doubling
    // it would corrupt the very paths it looks like it is protecting.
    CHECK(escape("dir\\file.hpp") == "dir\\file.hpp");
}

TEST_CASE("depfile::format - one target, one prerequisite per line, then the -MP block") {
    const std::string fragment = format({"wording/expected.tex"}, {"include/a.hpp", "include/detail/b.hpp"});
    CHECK(fragment == "wording/expected.tex: \\\n"
                      "  include/a.hpp \\\n"
                      "  include/detail/b.hpp\n"
                      "\n"
                      "include/a.hpp:\n"
                      "include/detail/b.hpp:\n");
}

TEST_CASE("depfile::format - several targets share one rule") {
    // The whole and the pieces come out of one invocation, so they are one
    // rule's targets and not several rules that would each re-run it.
    const std::string fragment = format({"w/a.tex", "w/b.tex"}, {"include/a.hpp"});
    CHECK(fragment == "w/a.tex w/b.tex: \\\n"
                      "  include/a.hpp\n"
                      "\n"
                      "include/a.hpp:\n");
}

TEST_CASE("depfile::format - no prerequisites is a bare rule with no -MP block") {
    CHECK(format({"w/a.tex"}, {}) == "w/a.tex:\n");
}

TEST_CASE("depfile::format - no targets is no fragment at all") {
    // Writing only the -MP block would declare every header phony, which is to
    // say would quietly stop the paper from ever rebuilding.
    CHECK(format({}, {"include/a.hpp"}) == "");
    CHECK(format({}, {}) == "");
}

TEST_CASE("depfile::format - targets and prerequisites are both escaped") {
    const std::string fragment = format({"out dir/a.tex"}, {"in dir/b.hpp"});
    CHECK(fragment == "out\\ dir/a.tex: \\\n"
                      "  in\\ dir/b.hpp\n"
                      "\n"
                      "in\\ dir/b.hpp:\n");
}
