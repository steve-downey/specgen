// tests/beman/specgen/foundation/recursion_schemes.test.cpp      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// fold_with identity/composition law tests on
// a toy rose tree. This exercises the vendored recursion verb through the
// explicit-parameter tier only (decision no-typeclass-objects) — a
// hand-written base functor, fmap, and
// projection, with no dependency on tree_algorithms' lookup tier (Fix,
// functor_typeclass, RoseTreeFix). It proves the seam the node-base-functor
// decision relies on: fold a tree
// in its own representation, const, no Fix materialized.

#include <beman/tree_algorithms/recursion_schemes.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>
#include <vector>

using beman::tree_algorithms::fold_with;

namespace {

// The user's own tree: a value and arbitrarily many children, owning them
// through std::vector (no Box, no Fix — exactly specgen's ir::Node situation).
struct Rose {
    int               value;
    std::vector<Rose> children;

    bool operator==(const Rose&) const = default;
};

// One layer of the tree, children left abstract as handle type A — the base
// functor fold_with folds against.
template <typename A>
struct RoseF {
    using child_type = A;
    int            value;
    std::vector<A> children;
};

// project: expose one layer of a Rose, children handed out as cheap handles the
// projection itself accepts on recursion (reference_wrapper converts back to
// const Rose&). Tree -> F<Handle>, per fold_with's contract.
constexpr auto project = [](const Rose& node) {
    RoseF<std::reference_wrapper<const Rose>> layer{node.value, {}};
    layer.children.reserve(node.children.size());
    for (const Rose& child : node.children) {
        layer.children.emplace_back(child);
    }
    return layer;
};

// fmap: map the recursive positions (children) of one layer, value untouched.
// (Fn, F<A>) -> F<B>.
constexpr auto fmap = [](const auto& fn, const auto& layer) {
    using A = typename std::remove_cvref_t<decltype(layer)>::child_type;
    using B = std::remove_cvref_t<std::invoke_result_t<decltype(fn), const A&>>;
    RoseF<B> out{layer.value, {}};
    out.children.reserve(layer.children.size());
    for (const A& child : layer.children) {
        out.children.push_back(fn(child));
    }
    return out;
};

// A small fixed tree:            1
//                              / | \
//                             2  3  4
//                                |
//                                5
const Rose sample{1, {Rose{2, {}}, Rose{3, {Rose{5, {}}}}, Rose{4, {}}}};

} // namespace

TEST_CASE("recursion_schemes - fold_with computes over the tree's own representation") {
    // Sum every value: value + sum of already-folded children.
    auto sum = [](const RoseF<int>& layer) {
        int acc = layer.value;
        for (int child : layer.children) {
            acc += child;
        }
        return acc;
    };
    CHECK(fold_with<int>(sum, fmap, project, sample) == 1 + 2 + 3 + 4 + 5);
}

TEST_CASE("recursion_schemes - identity law: folding with the constructor rebuilds the tree") {
    // The reflection law: fold_with with the embedding algebra (rebuild a Rose
    // from a folded layer) is the identity on the tree.
    auto rebuild = [](const RoseF<Rose>& layer) { return Rose{layer.value, layer.children}; };
    CHECK(fold_with<Rose>(rebuild, fmap, project, sample) == sample);
}

TEST_CASE("recursion_schemes - composition/fusion law") {
    // Fold fusion: for a strict h and algebras f, g with h(f x) == g(fmap h x),
    // then h . fold_with(f) == fold_with(g). Concretely, h = (·2) distributes
    // over the sum algebra f iff the node value is also doubled in g.
    auto f = [](const RoseF<int>& layer) {
        int acc = layer.value;
        for (int child : layer.children) {
            acc += child;
        }
        return acc;
    };
    auto g = [](const RoseF<int>& layer) {
        int acc = 2 * layer.value;
        for (int child : layer.children) {
            acc += child; // children are already the fused (doubled) results
        }
        return acc;
    };
    const int folded_then_scaled = 2 * fold_with<int>(f, fmap, project, sample);
    const int fused              = fold_with<int>(g, fmap, project, sample);
    CHECK(fused == folded_then_scaled);
}

TEST_CASE("recursion_schemes - a leaf folds to its own value") {
    const Rose leaf{42, {}};
    auto       sum = [](const RoseF<int>& layer) {
        int acc = layer.value;
        for (int child : layer.children) {
            acc += child;
        }
        return acc;
    };
    CHECK(fold_with<int>(sum, fmap, project, leaf) == 42);
}
