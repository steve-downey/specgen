// tests/beman/specgen/frontend/draft_format.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/draft_format.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <ranges>
#include <optional>
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

TEST_CASE("draft_format - item declarations use the approved template layout") {
    CHECK(frontend::format_draft("template<class T> void f(T const&);", 0, frontend::DraftContext::ItemDecl) ==
          "template<class T>\n  void f(const T&);");
    CHECK(frontend::format_draft("template<class T> requires C<T> void f(T);", 0, frontend::DraftContext::ItemDecl) ==
          "template<class T>\n  requires C<T>\n  void f(T);");
    CHECK(frontend::format_draft(
              "template<class T> static constexpr bool callable = true;", 0, frontend::DraftContext::ItemDecl) ==
          "template<class T>\n  static constexpr bool callable = true;");
    CHECK(frontend::format_draft(
              "template<template<class> class C, class T> using value = C<T>;", 0, frontend::DraftContext::ItemDecl) ==
          "template<template<class> class C, class T>\n  using value = C<T>;");
    CHECK(frontend::format_draft("template<class T> class box;", 0, frontend::DraftContext::ItemDecl) ==
          "template<class T>\n  class box;");
}

TEST_CASE("draft_format - item declarations fit within 120 columns after indentation") {
    const auto text =
        frontend::format_draft("template<class T, class U, class... Args> constexpr explicit "
                               "move_only_function(in_place_type_t<T>, initializer_list<U> ilist, Args&&... args);",
                               0,
                               frontend::DraftContext::ItemDecl);
    CHECK(text == "template<class T, class U, class... Args>\n  constexpr explicit "
                  "move_only_function(in_place_type_t<T>, initializer_list<U> ilist, Args&&... args);");
}

TEST_CASE("draft_format - item indentation precedes span recovery") {
    const std::map<std::string, SpanInfo> sentinels = {
        {span_sentinel(0), SpanInfo{ir::SpanKind::ExposId, "callable", "callable"}},
    };
    const auto code = frontend::format_and_recover("template<class T> bool " + span_sentinel(0) + ";",
                                                   sentinels,
                                                   std::nullopt,
                                                   0,
                                                   frontend::DraftContext::ItemDecl);
    CHECK(code.text == "template<class T>\n  bool callable;");
    REQUIRE(code.spans.size() == 1);
    CHECK(code.text.substr(code.spans.front().begin, code.spans.front().end - code.spans.front().begin) == "callable");
}

TEST_CASE("draft_format - declarations exceeding 120 columns still wrap") {
    const auto text =
        frontend::format_draft("template<class T> void configure(const T& first_parameter_with_a_long_name, const T& "
                               "second_parameter_with_a_long_name, const T& third_parameter_with_a_long_name, const "
                               "T& fourth_parameter_with_a_long_name);",
                               0,
                               frontend::DraftContext::ItemDecl);
    CHECK(std::ranges::all_of(text | std::views::split('\n'),
                              [](auto line) { return std::ranges::distance(line) <= 120; }));
    CHECK(std::ranges::count(text, '\n') > 1);
}
