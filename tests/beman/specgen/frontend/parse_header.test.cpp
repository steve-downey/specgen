// tests/beman/specgen/frontend/parse_header.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/parse_header.hpp>
#include <beman/specgen/frontend/parse_header.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace frontend = beman::specgen::frontend;

namespace {

const std::string kCorpusHeader = std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_sample.hpp";

} // namespace

TEST_CASE("parse_header - a corpus header parses cleanly") {
    const frontend::ParsedHeader parsed = frontend::parse_header(kCorpusHeader, {});
    REQUIRE(parsed.ast != nullptr);
    CHECK_FALSE(parsed.had_error);
}

TEST_CASE("parse_header - a header it cannot read yields no AST") {
    CHECK(frontend::parse_header("/nonexistent/nowhere.hpp", {}).ast == nullptr);
}

TEST_CASE("parse_header - the files a parse read lead with the header itself") {
    const frontend::ParsedHeader parsed = frontend::parse_header(kCorpusHeader, {});
    REQUIRE(parsed.ast != nullptr);
    const auto sources = frontend::parse_sources(kCorpusHeader, *parsed.ast);
    REQUIRE_FALSE(sources.empty());
    CHECK(sources.front() == kCorpusHeader);
}

TEST_CASE("parse_header - the unparsed-header failure names the file") {
    CHECK(frontend::unparsed_header("missing.hpp").message.find("'missing.hpp'") != std::string::npos);
}
