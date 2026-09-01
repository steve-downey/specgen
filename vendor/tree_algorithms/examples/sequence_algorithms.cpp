// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Sequence algorithms that never see a shape.
//
// A sequence-like tree — the fringe tree here, Paper C's FingerTree at
// industrial strength — internalizes its own structure: concat decides
// where branches go, rebalancing (when the representation does any) is
// the type's private business, and the only observable is the element
// sequence. Equivalence is QUOTIENT equivalence: two trees are the same
// sequence if they flatten to the same elements, whatever their shapes.
//
// This example demonstrates that the quotient interface is enough, and
// where each operation comes from. Construction and decomposition are
// written against five operations only — empty, leaf, concat, view_l,
// view_r (the fingertree vocabulary) — with no access to branches,
// measures, or balance: cons, snoc, from_range, reversed, and sequence
// equality derive there. Elementwise CONSUMPTION does not come from the
// views at all: a tree is a Foldable, fold_map is Foldable's primitive
// (and this repository's proposed spelling of it), and fold_left below
// is DERIVED from fold_map — the same left-fold-as-program derivation
// Paper C's Foldable base ships as LeftFoldProgram. One fold_left, one
// home; the view-driven walk appears only as the law check that the
// two ways of reading the sequence agree.
//
// The closing check is the payoff: the same sequence built snoc-wise
// (left-leaning) and cons-wise (right-leaning) has visibly different
// shapes under a structural fold, yet every sequence algorithm — and
// every fold_map, because monoid associativity quotients out the
// bracketing — agrees. Shape may vary freely behind the interface;
// that is what lets a real implementation rebalance.
//
// The concept and derived algorithms are example-local evidence, not
// proposed API; their names are placeholders, not decisions.

#include <beman/tree_algorithms/fringe_tree.hpp>

#include <beman/tree_algorithms/fold_map.hpp>
#include <beman/tree_algorithms/fold_map_lookup.hpp>
#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <concepts>
#include <functional>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using beman::tree_algorithms::fold_map;
using beman::tree_algorithms::fold_with;
using beman::tree_algorithms::fringe_tree_project;
using beman::tree_algorithms::FringeBranch;
using beman::tree_algorithms::FringeEmpty;
using beman::tree_algorithms::FringeLeaf;
using beman::tree_algorithms::FringeTree;
using beman::tree_algorithms::FringeTreeF;
using beman::tree_algorithms::functor_typeclass;
using beman::tree_algorithms::overloaded;
using beman::tree_algorithms::view_l;
using beman::tree_algorithms::view_r;

// The quotient interface: everything a sequence-generic algorithm may
// touch. Note what is absent — branch access, measures, balance.
template <typename S>
concept persistent_sequence = requires(const S& s, typename S::value_type v) {
    { S::empty() } -> std::same_as<S>;
    { S::leaf(std::move(v)) } -> std::same_as<S>;
    { S::concat(s, s) } -> std::same_as<S>;
    { view_l(s)->d_value } -> std::convertible_to<typename S::value_type>;
    { view_l(s)->d_rest } -> std::convertible_to<S>;
    { view_r(s)->d_value } -> std::convertible_to<typename S::value_type>;
    { view_r(s)->d_rest } -> std::convertible_to<S>;
};

// Construction derives from leaf + concat.
template <persistent_sequence S>
auto cons(typename S::value_type x, const S& s) -> S {
    return S::concat(S::leaf(std::move(x)), s);
}

template <persistent_sequence S>
auto snoc(const S& s, typename S::value_type x) -> S {
    return S::concat(s, S::leaf(std::move(x)));
}

template <persistent_sequence S, typename Range>
auto from_range(const Range& values) -> S {
    S out = S::empty();
    for (const auto& v : values) {
        out = snoc(out, v);
    }
    return out;
}

// Consumption comes from Foldable, not from the views. fold_left is
// fold_map with the left-fold-program monoid: each element maps to
// "feed me to the accumulator", programs combine by left-to-right
// composition, identity is the do-nothing program, and the assembled
// program runs on the initial value. (Haskell derives foldl from
// foldMap through Dual/Endo the same way; Paper C's foldable.hpp names
// the monoid LeftFoldProgram.) The tree's registered projection and
// layer fold do the walking — no view, no shape, no new machinery.
template <typename Result, typename S, typename Step>
auto fold_left(const Result& init, const Step& step, const S& s) -> Result {
    using Program = std::function<Result(Result)>;
    auto lift     = [&step](const typename S::value_type& v) -> Program {
        return [&step, v](Result acc) { return step(std::move(acc), v); };
    };
    auto compose_ltr = [](const Program& f, const Program& g) -> Program {
        return [f, g](Result acc) { return g(f(std::move(acc))); };
    };
    Program identity = [](Result acc) { return acc; };
    return fold_map<Program>(lift, compose_ltr, identity, s)(init);
}

template <persistent_sequence S>
auto to_vector(const S& s) -> std::vector<typename S::value_type> {
    using T = typename S::value_type;
    return fold_left<std::vector<T>>(
        std::vector<T>{},
        [](std::vector<T> acc, const T& v) {
            acc.push_back(v);
            return acc;
        },
        s);
}

template <persistent_sequence S>
auto reversed(const S& s) -> S {
    return fold_left<S>(S::empty(), [](const S& acc, const typename S::value_type& v) { return cons(v, acc); }, s);
}

// Equality on the quotient: same sequence, shapes never consulted.
template <persistent_sequence S>
auto sequence_equal(S a, S b) -> bool {
    for (;;) {
        auto va = view_l(a);
        auto vb = view_l(b);
        if (!va || !vb) {
            return !va && !vb;
        }
        if (!(va->d_value == vb->d_value)) {
            return false;
        }
        a = std::move(va->d_rest);
        b = std::move(vb->d_rest);
    }
}

// A structural fold that DOES see shape, for the contrast below.
static auto fringe_shape(const FringeTree<int>& t) -> std::string {
    auto algebra = [](const FringeTreeF<int, std::string>& layer) -> std::string {
        return std::visit(overloaded{
                              [](const FringeEmpty&) -> std::string { return "()"; },
                              [](const FringeLeaf<int>& l) { return std::to_string(l.value); },
                              [](const FringeBranch<std::string>& b) { return "(" + b.left + " " + b.right + ")"; },
                          },
                          layer);
    };
    const auto& fmap_fn = [](auto&& fn, const auto& layer) {
        using Layer = std::remove_cvref_t<decltype(layer)>;
        return functor_typeclass<Layer>.fmap(std::forward<decltype(fn)>(fn), layer);
    };
    return fold_with<std::string>(algebra, fmap_fn, fringe_tree_project, &t);
}

int main() {
    using Tree = FringeTree<int>;

    // The same sequence, two builds: snoc-wise leans left, cons-wise
    // leans right. A structural fold tells them apart —
    auto by_snoc = from_range<Tree>(std::vector<int>{1, 2, 3, 4});
    auto by_cons = cons(1, cons(2, cons(3, cons<Tree>(4, Tree::empty()))));

    std::println("snoc-built shape: {}", fringe_shape(by_snoc));
    std::println("cons-built shape: {}", fringe_shape(by_cons));
    bool ok = fringe_shape(by_snoc) != fringe_shape(by_cons);

    // — while everything written against the quotient interface agrees.
    ok = ok && sequence_equal(by_snoc, by_cons);
    ok = ok && to_vector(by_snoc) == std::vector<int>{1, 2, 3, 4};
    ok = ok && to_vector(by_cons) == std::vector<int>{1, 2, 3, 4};

    // Views from either end; the rest is specified as a sequence only.
    auto l = view_l(by_snoc);
    auto r = view_r(by_snoc);
    ok     = ok && l && l->d_value == 1 && to_vector(l->d_rest) == std::vector<int>{2, 3, 4};
    ok     = ok && r && r->d_value == 4 && to_vector(r->d_rest) == std::vector<int>{1, 2, 3};

    // Derived algorithms stay on the quotient.
    ok = ok && to_vector(reversed(by_snoc)) == std::vector<int>{4, 3, 2, 1};

    // The law that ties the two readings together. fold_left comes from
    // Foldable (derived from fold_map, so it runs over the structure);
    // the raw view_l walk below reads the sequence one decomposition at
    // a time. They must agree — and a non-commutative step (string
    // append, DEV-01) certifies both are left-to-right. fold_map itself
    // gives the same answer over both shapes because monoid
    // associativity is exactly shape-independence of bracketing.
    auto show   = [](int x) { return std::to_string(x); };
    auto append = [&](const std::string& acc, int v) { return acc + show(v); };

    auto via_foldable  = [&](const Tree& t) { return fold_left<std::string>(std::string{}, append, t); };
    auto via_view_walk = [&](Tree t) {
        std::string acc;
        while (auto v = view_l(t)) {
            acc = append(acc, v->d_value);
            t   = std::move(v->d_rest);
        }
        return acc;
    };
    auto sconcat      = [](const std::string& a, const std::string& b) { return a + b; };
    auto via_fold_map = [&](const Tree& t) { return fold_map<std::string>(show, sconcat, std::string{}, t); };

    for (const Tree& t : {by_snoc, by_cons}) {
        ok = ok && via_foldable(t) == "1234" && via_view_walk(t) == "1234" && via_fold_map(t) == "1234";
    }

    std::println("all checks passed: {}", ok);
    return ok ? 0 : 1;
}
