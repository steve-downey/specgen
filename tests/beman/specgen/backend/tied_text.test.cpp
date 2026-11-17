// tests/beman/specgen/backend/tied_text.test.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/tied_text.hpp>
#include <beman/specgen/backend/tied_text.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beman::specgen::ir;
namespace backend = beman::specgen::backend;

TEST_CASE("tied_text - a tied space is the target's no-break space, and no other hint is written") {
    CHECK(backend::tied_text(TextInline{"Within subclause "}, "&nbsp;") == "Within subclause ");
    CHECK(backend::tied_text(TextInline{"Within subclause ", {{16, LayoutHintKind::Tie}}}, "&nbsp;") ==
          "Within subclause&nbsp;");
    CHECK(backend::tied_text(TextInline{" is in Table 4, row 2",
                                        {{0, LayoutHintKind::Tie},
                                         {4, LayoutHintKind::Hyphen},
                                         {12, LayoutHintKind::Tie},
                                         {19, LayoutHintKind::Tie}}},
                             "\\nbsp{}") == "\\nbsp{}is in Table\\nbsp{}4, row\\nbsp{}2");
    // A tie that is not on a space has nothing to stand in place of.
    CHECK(backend::tied_text(TextInline{"ab", {{1, LayoutHintKind::Tie}, {2, LayoutHintKind::Tie}}}, "&nbsp;") ==
          "ab");
}
