// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A classic binary search tree, implemented natively on Fix.
//
// binary_tree_adapt.cpp adapts a tree that already exists; this example
// answers the other question: what does it look like to *build* a working
// tree on the machinery itself? The representation is BinaryTreeFix<int>
// — Fix over the same BinaryTreeF layer the adapter uses — and a whole
// tree is Box<BinaryTreeFix<int>>, so "possibly empty tree" and "child
// slot" are literally the same type: a disengaged Box is the empty tree.
//
// The honest division of labor, which the paper should state plainly:
//
//   - unfold_fix builds the tree (balanced, from a sorted range);
//   - fold_fix / fold_map consume the whole tree (flatten, height);
//   - insert, contains, and erase are plain structural recursion over
//     wrap_fix/unwrap_fix. Search PRUNES — it visits one path and
//     ignores the rest — and a fold by definition visits everything.
//     Fix does not take ordinary recursion away; the isomorphism
//     boundary is the whole interface.
//
// Persistence here is by deep copy: Box copies its pointee, so the
// untouched subtrees of an insert are copied, not shared (Decision 6 —
// value semantics, costs stated plainly). Structural sharing is the
// shared_ptr BinaryTree's department.
//
// The closing check ties into the quotient story told by the fringe
// tree: the insertion-order tree and the balanced unfold_fix tree have
// different shapes, but flatten to the same sorted sequence — in-order
// traversal quotients away the shape.

#include <beman/tree_algorithms/binary_tree.hpp>

#include <algorithm>
#include <array>
#include <print>
#include <string>
#include <utility>
#include <vector>

using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::BinaryTreeFix;
using beman::tree_algorithms::BinaryTreeLayer;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::child_slot_t;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::make_box;
using beman::tree_algorithms::make_slot;
using beman::tree_algorithms::unfold_fix;
using beman::tree_algorithms::unwrap_fix;
using beman::tree_algorithms::wrap_fix;

// The tree of the example: values at every node, Fix-native.
using SearchTree = BinaryTreeFix<int>;
// A possibly-empty (sub)tree; disengaged Box = empty. The same type a
// BinaryTreeF child slot holds.
using SubTree  = Box<SearchTree>;
using IntLayer = BinaryTreeLayer<int>;

/** Assemble one node from a value and two possibly-empty subtrees. */
constexpr auto node(int value, SubTree left, SubTree right) -> SearchTree {
    return wrap_fix<IntLayer::template F>(BinaryTreeF<int, SearchTree>{value, std::move(left), std::move(right)});
}

/** Insert @p v, returning the new tree; the input is untouched
 * (persistent by deep copy). Duplicates are dropped: set semantics. */
constexpr auto insert(const SubTree& tree, int v) -> SubTree {
    if (!tree) {
        return make_box<SearchTree>(node(v, SubTree{}, SubTree{}));
    }
    const BinaryTreeF<int, SearchTree>& layer = unwrap_fix(*tree);
    if (v < layer.value) {
        return make_box<SearchTree>(node(layer.value, insert(layer.left, v), layer.right));
    }
    if (layer.value < v) {
        return make_box<SearchTree>(node(layer.value, layer.left, insert(layer.right, v)));
    }
    return tree;
}

/** Membership. Plain recursion, deliberately not a fold: it follows one
 * path and never looks at the pruned subtrees. */
constexpr auto contains(const SubTree& tree, int v) -> bool {
    if (!tree) {
        return false;
    }
    const BinaryTreeF<int, SearchTree>& layer = unwrap_fix(*tree);
    if (v < layer.value) {
        return contains(layer.left, v);
    }
    if (layer.value < v) {
        return contains(layer.right, v);
    }
    return true;
}

/** Least value; precondition: nonempty. */
constexpr auto min_value(const SearchTree& tree) -> int {
    const BinaryTreeF<int, SearchTree>& layer = unwrap_fix(tree);
    return layer.left ? min_value(*layer.left) : layer.value;
}

/** Erase @p v if present: the textbook three cases, the two-child case
 * replacing the node's value with its in-order successor. */
constexpr auto erase(const SubTree& tree, int v) -> SubTree {
    if (!tree) {
        return SubTree{};
    }
    const BinaryTreeF<int, SearchTree>& layer = unwrap_fix(*tree);
    if (v < layer.value) {
        return make_box<SearchTree>(node(layer.value, erase(layer.left, v), layer.right));
    }
    if (layer.value < v) {
        return make_box<SearchTree>(node(layer.value, layer.left, erase(layer.right, v)));
    }
    if (!layer.left) {
        return layer.right;
    }
    if (!layer.right) {
        return layer.left;
    }
    int successor = min_value(*layer.right);
    return make_box<SearchTree>(node(successor, layer.left, erase(layer.right, successor)));
}

/** In-order flatten via fold_map — the registered BinaryTreeF layer fold
 * supplies the in-order contract; vector concatenation is the
 * (non-commutative) monoid, so a wrong order is a wrong answer. */
constexpr auto flatten(const SubTree& tree) -> std::vector<int> {
    if (!tree) {
        return {};
    }
    auto single  = [](int v) -> std::vector<int> { return {v}; };
    auto combine = [](const std::vector<int>& a, const std::vector<int>& b) -> std::vector<int> {
        std::vector<int> out = a;
        out.insert(out.end(), b.begin(), b.end());
        return out;
    };
    return fold_map<std::vector<int>>(single, combine, std::vector<int>{}, *tree);
}

/** Height via fold_fix: one whole-tree consumption that IS a fold. */
constexpr auto height(const SubTree& tree) -> int {
    if (!tree) {
        return 0;
    }
    auto algebra = [](const BinaryTreeF<int, int>& layer) -> int {
        int left  = layer.left ? *layer.left : 0;
        int right = layer.right ? *layer.right : 0;
        return 1 + std::max(left, right);
    };
    return fold_fix<int>(algebra, *tree);
}

/** Shape rendering, the adapter chapter's order-sensitive algebra:
 * "(left value right)" with "." for an absent child. */
constexpr auto shape(const SubTree& tree) -> std::string {
    if (!tree) {
        return ".";
    }
    auto algebra = [](const BinaryTreeF<int, std::string>& layer) -> std::string {
        auto child = [](const auto& c) { return c ? *c : std::string("."); };
        return "(" + child(layer.left) + " " + std::to_string(layer.value) + " " + child(layer.right) + ")";
    };
    return fold_fix<std::string>(algebra, *tree);
}

/** Balanced build from a sorted range of ints via unfold_fix: the
 * coalgebra splits [lo, hi) at its midpoint. */
constexpr auto balanced(const std::vector<int>& sorted) -> SubTree {
    if (sorted.empty()) {
        return SubTree{};
    }
    using Range    = std::pair<std::size_t, std::size_t>;
    auto coalgebra = [&sorted](const Range& r) -> BinaryTreeF<int, Range> {
        auto [lo, hi]   = r;
        std::size_t mid = lo + (hi - lo) / 2;
        return BinaryTreeF<int, Range>{sorted[mid],
                                       mid > lo ? make_slot<Range>(Range{lo, mid}) : child_slot_t<Range>{},
                                       hi > mid + 1 ? make_slot<Range>(Range{mid + 1, hi}) : child_slot_t<Range>{}};
    };
    return make_box<SearchTree>(unfold_fix<IntLayer::template F>(coalgebra, Range{0U, sorted.size()}));
}

// The whole lifecycle is constexpr: build by insertion, query, erase,
// flatten — evaluated here at compile time (DEV-04 discipline applied to
// the example itself).
static_assert([] {
    SubTree t{};
    for (int v : {5, 2, 8, 1, 3}) {
        t = insert(t, v);
    }
    bool ok = contains(t, 3) && !contains(t, 4);
    t       = erase(t, 2); // two children: successor case
    ok      = ok && !contains(t, 2) && contains(t, 1) && contains(t, 3);
    return ok && flatten(t) == std::vector<int>{1, 3, 5, 8};
}());

int main() {
    // Build by insertion, in scrambled order.
    SubTree by_insertion{};
    for (int v : {5, 2, 8, 1, 3, 7, 9, 6, 4}) {
        by_insertion = insert(by_insertion, v);
    }

    auto flat = flatten(by_insertion);
    std::println("flatten after inserts: {}", flat);
    bool ok = std::ranges::is_sorted(flat) && flat == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9};

    ok = ok && contains(by_insertion, 7) && !contains(by_insertion, 10);

    // Erase an inner node (2: two children) and the root (5).
    auto pruned = erase(erase(by_insertion, 2), 5);
    std::println("flatten after erases:  {}", flatten(pruned));
    ok = ok && flatten(pruned) == std::vector<int>{1, 3, 4, 6, 7, 8, 9};

    // The same sequence, built balanced by unfold_fix: different shape,
    // same flatten — in-order traversal quotients away the shape.
    auto by_unfold = balanced(std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9});
    std::println("insertion shape:       {}", shape(by_insertion));
    std::println("balanced shape:        {}", shape(by_unfold));
    std::println("heights:               {} vs {}", height(by_insertion), height(by_unfold));
    ok = ok && shape(by_insertion) != shape(by_unfold) && flatten(by_unfold) == flatten(by_insertion) &&
         height(by_unfold) <= height(by_insertion);

    std::println("all checks passed: {}", ok);
    return ok ? 0 : 1;
}
