// tests/beman/specgen/ir_fold.test.cpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The base-functor gate: node_project/node_embed round-trip on
// every ir::Node alternative, node_fmap's identity/composition laws (the
// toy-Rose shape, exercised here on NodeF directly rather than a throwaway
// tree), and decision node-base-functor's proof obligation -- JSON emission
// re-expressed as a fold_with algebra over NodeF, pinned against the
// production emitter on a nested multi-level document.

#include <beman/specgen/ir_fold.hpp>
#include <beman/specgen/ir_fold.hpp> // Re-inclusion verification

#include <beman/specgen/foundation/json_writer.hpp>
#include <beman/specgen/foundation/overloaded.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using namespace beman::specgen::ir;

namespace {

std::string to_json(const Node& node) { return emit_json(node); }

// node_project's Handle type, copied out to an owned Node -- the fmap
// function for the shallow round-trip test below.
Node copy_handle(const std::reference_wrapper<const Node>& handle) { return handle.get(); }

} // namespace

TEST_CASE("ir_fold - HeaderIsIdempotent") {
    // Placeholder: verifies header re-inclusion safety and build coherency.
    REQUIRE(true);
}

TEST_CASE("ir_fold - node_project/node_embed round-trip: Section") {
    Node original = Section{
        "widget.ctor",
        "Constructors",
        {Node{Synopsis{.name = {}, .code = {"widget() noexcept;", {}}, .roster = {}}},
         Node{FreeParagraph{{TextInline{"General."}}}}},
    };

    NodeF<Node> owned   = node_fmap(copy_handle, node_project(original));
    Node        rebuilt = node_embed(std::move(owned));

    CHECK(to_json(rebuilt) == to_json(original));
}

TEST_CASE("ir_fold - node_project/node_embed round-trip: Synopsis") {
    Node original = Synopsis{.name = {}, .code = {"int x;", {}}, .roster = {}};

    NodeF<Node> owned   = node_fmap(copy_handle, node_project(original));
    Node        rebuilt = node_embed(std::move(owned));

    CHECK(to_json(rebuilt) == to_json(original));
}

TEST_CASE("ir_fold - node_project/node_embed round-trip: SpecItem") {
    SpecItem item;
    item.decl.signatures.push_back(CodeText{"void f();", {}});
    Node original = item;

    NodeF<Node> owned   = node_fmap(copy_handle, node_project(original));
    Node        rebuilt = node_embed(std::move(owned));

    CHECK(to_json(rebuilt) == to_json(original));
}

TEST_CASE("ir_fold - node_project/node_embed round-trip: FreeParagraph") {
    Node original = FreeParagraph{{TextInline{"Some prose."}}};

    NodeF<Node> owned   = node_fmap(copy_handle, node_project(original));
    Node        rebuilt = node_embed(std::move(owned));

    CHECK(to_json(rebuilt) == to_json(original));
}

TEST_CASE("ir_fold - node_fmap identity law: SectionF") {
    NodeF<int> layer = SectionF<int>{"sec.stable", "Sec Title", {1, 2, 3}};

    auto        mapped = node_fmap([](const int& x) { return x; }, layer);
    const auto& out    = std::get<SectionF<int>>(mapped);

    CHECK(out.stable_name == "sec.stable");
    CHECK(out.title == "Sec Title");
    CHECK(out.children == std::vector<int>{1, 2, 3});
}

TEST_CASE("ir_fold - node_fmap identity law: leaf alternatives pass through") {
    NodeF<int> synopsis_layer  = Synopsis{.name = {}, .code = {"leaf code", {}}, .roster = {}};
    auto       mapped_synopsis = node_fmap([](const int& x) { return x; }, synopsis_layer);
    REQUIRE(std::holds_alternative<Synopsis>(mapped_synopsis));
    CHECK(std::get<Synopsis>(mapped_synopsis).code.text == "leaf code");

    NodeF<int> para_layer  = FreeParagraph{{TextInline{"leaf text"}}};
    auto       mapped_para = node_fmap([](const int& x) { return x; }, para_layer);
    REQUIRE(std::holds_alternative<FreeParagraph>(mapped_para));
}

TEST_CASE("ir_fold - node_fmap composition law") {
    NodeF<int> layer = SectionF<int>{"s", "T", {1, 2, 3}};
    auto       f     = [](const int& x) { return x + 1; };
    auto       g     = [](const int& x) { return x * 2; };

    auto lhs = node_fmap([&](const int& x) { return g(f(x)); }, layer); // fmap(g . f)
    auto rhs = node_fmap(g, node_fmap(f, layer));                       // fmap(g) . fmap(f)

    REQUIRE(std::holds_alternative<SectionF<int>>(lhs));
    REQUIRE(std::holds_alternative<SectionF<int>>(rhs));
    const auto& lhs_section = std::get<SectionF<int>>(lhs);
    const auto& rhs_section = std::get<SectionF<int>>(rhs);
    CHECK(lhs_section.stable_name == rhs_section.stable_name);
    CHECK(lhs_section.title == rhs_section.title);
    CHECK(lhs_section.children == rhs_section.children);
    CHECK(lhs_section.children == std::vector<int>{4, 6, 8});
}

namespace {

namespace foundation = beman::specgen::foundation;

// --- the node-base-functor proof obligation ---------------------------------
//
// JSON emission expressed as an algebra NodeF<std::string> -> std::string,
// where the carrier is "this node's own already-serialized JSON text". The
// shapes compose because JSON text is itself compositional: a child's
// serialized object splices verbatim into its parent's array.
//
// This lives in the test rather than on ir.cpp's emit path, deliberately.
// The proof obligation targets a hand-written recursive `if constexpr`
// chain -- exactly the shape NodeF exists to replace -- but emit_json(Node)
// carries no such recursion: the descriptor tables (decision
// json-single-schema) leave it a single non-recursive
// emit_variant call. Folding there would trade that one generic call for a
// bespoke Section case that has to reach around the descriptor engine,
// because foundation::emit_value(vector<T>) always treats an element as live
// data to serialize and has no way to accept an already-rendered blob. So
// the production path stays table-driven and the functor is proven here
// instead. The LaTeX backend is the acid test, and rendering is
// naturally fold-shaped in a way streaming JSON emission is not.
//
// The assertion is an *equivalence*: whatever the descriptor engine emits,
// the fold must reproduce byte for byte. Key names are spelled literally
// below on purpose -- json_descriptor<Node> and json_descriptor<Section> are
// specializations private to ir.cpp's translation unit, and a test that
// re-derived the schema from the very tables the production path reads could
// not detect a change in it. Spelled out, this test also pins the frozen
// JSON shape independently of ir.cpp.
struct EmitJsonAlgebra {
    // The one alternative carrying a recursive position: reopen the object
    // shape a Section serializes to, splicing each child's already-rendered
    // text into the "children" array rather than re-serializing the child.
    std::string operator()(const SectionF<std::string>& s) const {
        std::string out;
        {
            foundation::json_object obj(out);
            foundation::write_json_string("section", obj.key("type"));
            foundation::write_json_string(s.stable_name, obj.key("stable"));
            foundation::write_json_string(s.title, obj.key("title"));
            foundation::json_array items(obj.key("children"));
            // substrate generic algorithm: json_array places its separators
            // as a side effect of element() being called once per item, so
            // this is the array-emission primitive itself, not a fold in
            // disguise.
            for (const std::string& child_json : s.children)
                items.element() += child_json;
        }
        return out;
    }

    // The three leaf alternatives have no recursive position, so each one's
    // JSON is exactly what the production emitter produces for a Node
    // holding it -- reused, not re-derived.
    std::string operator()(const Synopsis& v) const { return to_json(Node{v}); }
    std::string operator()(const SpecItem& v) const { return to_json(Node{v}); }
    std::string operator()(const FreeParagraph& v) const { return to_json(Node{v}); }
};

std::string fold_to_json(const Node& node) {
    return beman::tree_algorithms::fold_with<std::string>(
        [](const NodeF<std::string>& layer) { return std::visit(foundation::overloaded{EmitJsonAlgebra{}}, layer); },
        node_fmap,
        node_project,
        node);
}

} // namespace

TEST_CASE("ir_fold - a fold_with algebra over NodeF reproduces the descriptor engine's JSON") {
    // A two-level tree exercising the one alternative fold_with actually
    // threads (Section) alongside all three leaves.
    Node inner = Section{
        "inner.stable",
        "Inner",
        {
            Node{Synopsis{.name = {}, .code = {"int x;", {}}, .roster = {}}},
            Node{FreeParagraph{{TextInline{"hello"}}}},
        },
    };
    Node root = Section{
        "outer.stable",
        "Outer",
        {inner, Node{SpecItem{}}},
    };

    const std::string expected =
        "{\"type\":\"section\",\"stable\":\"outer.stable\",\"title\":\"Outer\",\"children\":["
        "{\"type\":\"section\",\"stable\":\"inner.stable\",\"title\":\"Inner\",\"children\":["
        "{\"type\":\"synopsis\",\"name\":\"\",\"code\":{\"text\":\"int x;\",\"spans\":[]},\"roster\":[]},"
        "{\"type\":\"para\",\"content\":[{\"t\":\"text\",\"text\":\"hello\"}]}"
        "]},"
        "{\"type\":\"item\",\"decl\":{\"signatures\":[],\"index\":[]},\"descr\":{\"elements\":[]}}"
        "]}";

    // The fold reproduces the schema spelled out above...
    CHECK(fold_to_json(root) == expected);

    // ...and, the load-bearing half, reproduces whatever the production
    // descriptor-driven emitter produces, byte for byte. This is what makes
    // the proof survive: a schema change that updates ir.cpp's tables and
    // this test's expected string in step would still have to keep the two
    // emission routes agreeing.
    CHECK(fold_to_json(root) == to_json(root));
}
