// tests/beman/specgen/backend/iref_spacing.test.cpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/backend/iref_spacing.hpp>
#include <beman/specgen/backend/iref_spacing.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

namespace backend = beman::specgen::backend;
namespace ir      = beman::specgen::ir;

namespace {

// Each piece as text, and a reference as its parenthesized stable name.
struct Render {
    std::string operator()(const ir::Inline& piece) const {
        if (const auto* text = std::get_if<ir::TextInline>(&piece))
            return text->text;
        if (const auto* ref = std::get_if<ir::RefInline>(&piece))
            return ref->bare ? '[' + ref->stable_name + ']' : "([" + ref->stable_name + "])";
        return "<run>";
    }
};

} // namespace

// Issue #408: the draft writes `movable\iref{x}`, and the macro prints the space.
TEST_CASE("iref_spacing - a reference directly after a word takes a space") {
    const ir::Paragraph para{ir::TextInline{"is movable"}, ir::RefInline{"dcl.init.general"}, ir::TextInline{"."}};
    CHECK(backend::join_with_iref_spacing(para, Render{}) == "is movable ([dcl.init.general]).");
}

TEST_CASE("iref_spacing - a space already there is the only one") {
    const ir::Paragraph para{ir::TextInline{"is movable "}, ir::RefInline{"dcl.init.general"}};
    CHECK(backend::join_with_iref_spacing(para, Render{}) == "is movable ([dcl.init.general])");
}

TEST_CASE("iref_spacing - a bare reference, and one that begins the text, take none") {
    CHECK(backend::join_with_iref_spacing(
              ir::Paragraph{ir::TextInline{"subclause"}, ir::RefInline{.stable_name = "x", .bare = true}}, Render{}) ==
          "subclause[x]");
    CHECK(backend::join_with_iref_spacing(ir::Paragraph{ir::RefInline{"x"}, ir::TextInline{" says"}}, Render{}) ==
          "([x]) says");
}

TEST_CASE("iref_spacing - a changed run that opens with a reference takes the space outside it") {
    const ir::Paragraph para{ir::TextInline{"is movable"}, ir::ChangedInline{ir::Change::Added, {ir::RefInline{"x"}}}};
    CHECK(backend::opens_with_iref(para[1]));
    CHECK(backend::join_with_iref_spacing(para, Render{}) == "is movable <run>");
}
