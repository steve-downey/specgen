// tests/beman/specgen/fragments.test.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/fragments.hpp>
#include <beman/specgen/fragments.hpp> // Re-inclusion verification

#include <beman/specgen/backend/latex.hpp>

#include <catch2/catch_test_macros.hpp>

#include <ranges>
#include <string>
#include <vector>

namespace latex = beman::specgen::backend::latex;
using namespace beman::specgen::fragments;
using namespace beman::specgen::ir;

namespace {

Node section(std::string stable, std::string title = "Constructors") {
    Section node;
    node.stable_name = std::move(stable);
    node.title       = std::move(title);
    return node;
}

Node synopsis(std::string name) {
    Synopsis node;
    node.name      = std::move(name);
    node.code.text = "class " + node.name + " {};";
    return node;
}

std::vector<std::string> names(const std::vector<Fragment>& fragments) {
    return fragments | std::views::transform(&Fragment::name) | std::ranges::to<std::vector>();
}

} // namespace

TEST_CASE("a section becomes a fragment named by its stable name") {
    Document document;
    document.nodes = {section("optional.ctor"), section("optional.assign", "Assignment")};

    const auto fragments = split(document);
    REQUIRE(fragments);
    CHECK(names(*fragments) == std::vector<std::string>{"optional.ctor", "optional.assign"});
    // Document order, not sorted order: the filesystem loses it and the
    // manifest is the only place it survives.
    CHECK(fragments->front().document.nodes.size() == 1);
}

TEST_CASE("nodes outside every section gather into one root fragment") {
    Document document;
    document.nodes = {synopsis("nullopt_t"),
                      synopsis("optional"),
                      section("optional.ctor"),
                      section("optional.observe", "Observers")};

    const auto fragments = split(document);
    REQUIRE(fragments);
    // The root fragment is placed where its first node is, so it leads here —
    // and its name is the prefix the two sections share.
    CHECK(names(*fragments) == std::vector<std::string>{"optional", "optional.ctor", "optional.observe"});
    CHECK(fragments->front().document.nodes.size() == 2);
}

TEST_CASE("a loose node after a section still joins the one root fragment") {
    Document document;
    document.nodes = {section("box.cons"), synopsis("box"), section("box.observe", "Observers")};

    const auto fragments = split(document);
    REQUIRE(fragments);
    CHECK(names(*fragments) == std::vector<std::string>{"box.cons", "box", "box.observe"});
}

TEST_CASE("a lone section names the root fragment by its parent") {
    // The common prefix of one name is that name, which is the path the
    // section itself claims; the derivation drops a component rather than
    // colliding.
    Document document;
    document.nodes = {synopsis("stack"), section("stack.access", "Element access")};

    const auto fragments = split(document);
    REQUIRE(fragments);
    CHECK(names(*fragments) == std::vector<std::string>{"stack", "stack.access"});
}

TEST_CASE("an explicit root name overrides the derivation") {
    Document document;
    document.nodes = {synopsis("optional"), section("optional.ctor")};

    const auto fragments = split(document, {.root = "optional.syn"});
    REQUIRE(fragments);
    CHECK(names(*fragments) == std::vector<std::string>{"optional.syn", "optional.ctor"});
}

TEST_CASE("sections sharing no prefix cannot name a root fragment") {
    Document document;
    document.nodes = {synopsis("gadget"), section("gadget.cons"), section("widget.cons")};

    const auto fragments = split(document);
    REQUIRE_FALSE(fragments);
    // The one failure a caller can fix by naming the fragment, flagged so the
    // driver can say which flag does that without this component knowing.
    CHECK(fragments.error().root_unnamed);

    const auto named = split(document, {.root = "demo.syn"});
    REQUIRE(named);
    CHECK(names(*named) == std::vector<std::string>{"demo.syn", "gadget.cons", "widget.cons"});
}

TEST_CASE("no loose nodes needs no root name") {
    // Nothing to name, so the derivation that would have failed above never
    // runs — a document of nothing but unrelated sections splits cleanly.
    Document document;
    document.nodes = {section("gadget.cons"), section("widget.cons")};

    const auto fragments = split(document);
    REQUIRE(fragments);
    CHECK(names(*fragments) == std::vector<std::string>{"gadget.cons", "widget.cons"});
}

TEST_CASE("an empty document splits into nothing") {
    const auto fragments = split(Document{});
    REQUIRE(fragments);
    CHECK(fragments->empty());
}

TEST_CASE("a section with no stable name is reported") {
    Document document;
    document.nodes = {section("", "Observers")};

    const auto fragments = split(document);
    REQUIRE_FALSE(fragments);
    CHECK(fragments.error().message == "the section 'Observers' has no stable name to derive a fragment path from");
    CHECK_FALSE(fragments.error().root_unnamed);
}

TEST_CASE("two fragments cannot claim one name") {
    Document document;
    document.nodes = {section("optional.ctor"), section("optional.ctor")};

    const auto fragments = split(document);
    REQUIRE_FALSE(fragments);
    CHECK(fragments.error().message == "two fragments would both be named 'optional.ctor'");

    // Same rule when the collision is between a section and an explicitly
    // named root fragment, which is the shape a careless --root produces.
    Document with_root;
    with_root.nodes     = {synopsis("optional"), section("optional.ctor")};
    const auto collided = split(with_root, {.root = "optional.ctor"});
    REQUIRE_FALSE(collided);
}

TEST_CASE("a stable name that cannot be a file name is refused") {
    CHECK(usable_as_file_name("optional.ctor"));
    CHECK(usable_as_file_name("expected.object.eq"));
    CHECK(usable_as_file_name("basic_string.cons"));
    CHECK(usable_as_file_name("c++.syn"));

    CHECK_FALSE(usable_as_file_name(""));
    CHECK_FALSE(usable_as_file_name("../etc/passwd"));
    CHECK_FALSE(usable_as_file_name("optional/ctor"));
    CHECK_FALSE(usable_as_file_name(".hidden"));
    CHECK_FALSE(usable_as_file_name("-flag"));
    CHECK_FALSE(usable_as_file_name("optional."));
    CHECK_FALSE(usable_as_file_name("optional ctor"));

    Document document;
    document.nodes       = {section("../escape")};
    const auto fragments = split(document);
    REQUIRE_FALSE(fragments);
    CHECK(fragments.error().message == "'../escape' cannot name a file, so it cannot name a fragment");
}

TEST_CASE("a fragment renders as the document it is") {
    // The property the split exists to have: a fragment goes through the
    // ordinary backend entry point, so its bytes are the whole document's
    // bytes for that section and no backend knows a fragment from a document.
    Document document;
    document.nodes = {section("optional.ctor", "Constructors"), section("optional.mod", "Modifiers")};

    const auto fragments = split(document);
    REQUIRE(fragments);
    const std::string whole = latex::render_to_string(document);
    CHECK(whole.contains(latex::render_to_string(fragments->front().document)));
    CHECK(whole.contains(latex::render_to_string(fragments->back().document)));
}
