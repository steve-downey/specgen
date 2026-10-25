// tests/beman/specgen/frontend/draft_format.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/draft_format.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;
using beman::specgen::span_sentinels::span_sentinel;
using beman::specgen::span_sentinels::SpanInfo;

// The goldens own the draft style's full effect; these pin the rules the
// style exists for, one at a time, on text small enough to read.

TEST_CASE("draft_format - references attach to the type and qualifiers go left") {
    CHECK(frontend::format_draft("void f(T const &x);", 0) == "void f(const T& x);");
}

TEST_CASE("draft_format - a requires-clause goes on its own indented line") {
    CHECK(frontend::format_draft("template <class T> requires C<T> void f(T x);", 0) ==
          "template<class T>\n  requires C<T>\nvoid f(T x);");
}

TEST_CASE("draft_format - an expression fragment keeps its layout and gains the draft's spelling") {
    CHECK(frontend::format_expr_fragment("requires(T const & t) { t.f(); }") == "requires(const T& t) { t.f(); }");
    CHECK(frontend::format_expr_fragment("is_integral_v<T>") == "is_integral_v<T>");
}

TEST_CASE("draft_format - formatting recovers the sentinels it was given") {
    const std::map<std::string, SpanInfo> sentinels = {
        {span_sentinel(0), SpanInfo{ir::SpanKind::ExposId, "val", "val"}},
    };
    const ir::CodeText code = frontend::format_and_recover("int  " + span_sentinel(0) + "( );", sentinels);
    CHECK(code.text == "int val();");
    REQUIRE(code.spans.size() == 1);
    CHECK(code.spans[0].payload == "val");
}

TEST_CASE("draft_format - a record template keeps its head on a line of its own") {
    CHECK(frontend::format_and_recover("template <class T> class box { };", {}, "class").text ==
          "template<class T>\nclass box {};");
}
