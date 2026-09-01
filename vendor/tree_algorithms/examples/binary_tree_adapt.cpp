// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Adapting a tree you don't own: BinaryTree<int> is built through its own
// factory functions, with the shared_ptr representation it has always
// had. to_fix presents it layer by layer as a base functor, the same
// fold_fix that runs over ExprF folds it with an order-sensitive algebra,
// and from_fix rebuilds the original representation. The verbs and the
// algebra never learn how the tree is really stored.

#include <beman/tree_algorithms/binary_tree.hpp>
#include <beman/tree_algorithms/box.hpp>
#include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

#include <print>
#include <string>

using beman::tree_algorithms::BinaryTree;
using beman::tree_algorithms::BinaryTreeF;
using beman::tree_algorithms::Box;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::from_fix;
using beman::tree_algorithms::to_fix;

int main() {
    // fba6bbba-6cd8-4f4f-ba18-b584cf672a79
    using BT = BinaryTree<int>;

    //        1
    //       . .
    //      2   3
    //     . .
    //    4   5
    auto tree = BT::node(1, BT::node(2, BT::leaf(4), BT::leaf(5)), BT::leaf(3));

    // Adapt: convert the shared_ptr tree to its Fix form, one layer at a
    // time. Only to_fix knows about shared_ptr.
    auto fixed = to_fix(tree);

    // An order-sensitive algebra: "(left value right)" with "." marking an
    // absent child. Swapping child order or dropping a level changes the
    // string, so the printed shape pins the traversal exactly.
    auto shape_algebra = [](const BinaryTreeF<int, std::string>& layer) -> std::string {
        auto child = [](const auto& c) { return c ? *c : std::string("."); };
        return "(" + child(layer.left) + " " + std::to_string(layer.value) + " " + child(layer.right) + ")";
    };

    auto shape = fold_fix<std::string>(shape_algebra, fixed);
    std::println("shape via fold_fix:  {}", shape);

    // Round-trip: rebuild the original representation from the Fix form
    // (itself a fold), then fold the round-tripped tree again.
    BT   rebuilt     = from_fix(fixed);
    auto shape_again = fold_fix<std::string>(shape_algebra, to_fix(rebuilt));
    std::println("shape after round-trip: {}", shape_again);
    // fba6bbba-6cd8-4f4f-ba18-b584cf672a79 end

    const std::string expected = "(((. 4 .) 2 (. 5 .)) 1 (. 3 .))";
    bool              ok       = (shape == expected) && (shape_again == expected);
    std::println("round-trip preserves shape: {}", ok);
    return ok ? 0 : 1;
}
