// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The tree you wrote yesterday: unique_ptr children, not even copyable,
// no Fix, no Box, no typeclasses. Three ingredients — a base-functor
// layer describing one level, an fmap for that layer, and a projection
// handing out one layer at a time — and fold_with/unfold_with run over
// the tree in its own representation. Nothing is converted; no Fix is
// ever materialized. (Contrast binary_tree_adapt.cpp, which converts a
// shared_ptr tree to Fix form with to_fix and folds that.)

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <memory>
#include <optional>
#include <print>
#include <string>
#include <type_traits>
#include <utility>

using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::unfold_with;

// 04e450a4-3614-4f09-a185-1835f935673a
// Yesterday's tree.
struct Node {
    int                   value;
    std::unique_ptr<Node> left;  // null = absent
    std::unique_ptr<Node> right; // null = absent
};

auto leaf(int v) -> std::unique_ptr<Node> { return std::make_unique<Node>(Node{v, nullptr, nullptr}); }

auto node(int v, std::unique_ptr<Node> l, std::unique_ptr<Node> r) -> std::unique_ptr<Node> {
    return std::make_unique<Node>(Node{v, std::move(l), std::move(r)});
}

// Ingredient one: one layer of the tree, child slots holding whatever
// handle type the projection deals in.
template <typename A>
struct NodeF {
    int              value;
    std::optional<A> left;
    std::optional<A> right;
};

// Ingredient two: fmap for that layer — apply a function to each engaged
// child slot, left before right; the value rides along.
inline constexpr auto fmap_node = [](auto&& fn, const auto& layer) {
    using A = std::remove_cvref_t<decltype(*layer.left)>;
    using B = std::remove_cvref_t<std::invoke_result_t<decltype(fn), const A&>>;
    return NodeF<B>{layer.value,
                    layer.left ? std::optional<B>{fn(*layer.left)} : std::optional<B>{},
                    layer.right ? std::optional<B>{fn(*layer.right)} : std::optional<B>{}};
};

// Ingredient three: the projection — expose one layer, children as raw
// non-owning pointers into the tree we already have.
inline constexpr auto project = [](const Node* n) -> NodeF<const Node*> {
    return {n->value,
            n->left ? std::optional<const Node*>{n->left.get()} : std::optional<const Node*>{},
            n->right ? std::optional<const Node*>{n->right.get()} : std::optional<const Node*>{}};
};
// 04e450a4-3614-4f09-a185-1835f935673a end

int main() {
    //        1
    //       . .
    //      2   3
    //     . .
    //    4   5
    auto tree = node(1, node(2, leaf(4), leaf(5)), leaf(3));

    // An order-sensitive algebra: "(left value right)" with "." marking
    // an absent child pins shape and traversal order exactly.
    auto shape_algebra = [](const NodeF<std::string>& layer) -> std::string {
        auto child = [](const std::optional<std::string>& c) { return c ? *c : std::string("."); };
        return "(" + child(layer.left) + " " + std::to_string(layer.value) + " " + child(layer.right) + ")";
    };

    auto shape = fold_with<std::string>(shape_algebra, fmap_node, project, tree.get());
    std::println("shape via fold_with:   {}", shape);

    // And the dual: build a balanced BST over [0, 7) directly into
    // unique_ptr nodes — a move-only carrier no copy-based conversion
    // pipeline could produce.
    using Range        = std::pair<int, int>;
    auto bst_coalgebra = [](const Range& r) -> NodeF<Range> {
        auto [lo, hi] = r;
        int mid       = lo + (hi - lo) / 2;
        return {mid,
                mid > lo ? std::optional<Range>{Range{lo, mid}} : std::optional<Range>{},
                hi > mid + 1 ? std::optional<Range>{Range{mid + 1, hi}} : std::optional<Range>{}};
    };
    auto embed = [](NodeF<std::unique_ptr<Node>>&& layer) -> std::unique_ptr<Node> {
        return std::make_unique<Node>(Node{layer.value,
                                           layer.left ? std::move(*layer.left) : nullptr,
                                           layer.right ? std::move(*layer.right) : nullptr});
    };

    auto built       = unfold_with<std::unique_ptr<Node>>(bst_coalgebra, fmap_node, embed, Range{0, 7});
    auto built_shape = fold_with<std::string>(shape_algebra, fmap_node, project, built.get());
    std::println("BST via unfold_with:   {}", built_shape);

    const std::string expected_shape = "(((. 4 .) 2 (. 5 .)) 1 (. 3 .))";
    const std::string expected_bst   = "(((. 0 .) 1 (. 2 .)) 3 ((. 4 .) 5 (. 6 .)))";
    bool              ok             = (shape == expected_shape) && (built_shape == expected_bst);
    std::println("shapes as expected: {}", ok);
    return ok ? 0 : 1;
}
