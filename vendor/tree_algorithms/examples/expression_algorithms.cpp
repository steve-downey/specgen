// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The algorithm-object story: `validate` and `transform_if_large` from the
// trees repository (smd::tree::algorithm, fixpoint_tree_algorithm.hpp),
// reworked against beman::tree_algorithms' expression tree.
//
// The library deliberately ships functor-only lookup (docs/DECISIONS.md,
// Decision 1: minimal algorithms core). The Foldable- and
// Traversable-shaped operations these algorithms compose are therefore
// EXAMPLE-LOCAL: defined in this translation unit, built on the public
// fold_fix verb, and not part of the library surface.

#include <beman/tree_algorithms/expression.hpp>
#include <beman/tree_algorithms/overloaded.hpp>
#include <beman/tree_algorithms/recursion_schemes_lookup.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <print>
#include <type_traits>
#include <utility>

namespace example {

using beman::tree_algorithms::Add;
using beman::tree_algorithms::add_node;
using beman::tree_algorithms::Const;
using beman::tree_algorithms::const_node;
using beman::tree_algorithms::eval;
using beman::tree_algorithms::Expr;
using beman::tree_algorithms::ExprF;
using beman::tree_algorithms::fold_fix;
using beman::tree_algorithms::Mul;
using beman::tree_algorithms::mul_node;
using beman::tree_algorithms::overloaded;

// a66c0122-f24d-4858-a802-cf76f87969e9
// ---------------------------------------------------------------------
// Example-local Foldable / Traversable maps for Expr.
//
// EXAMPLE-LOCAL, NOT LIBRARY SURFACE. Each operation is a thin algebra
// over the public fold_fix verb; the "elements" of an expression tree are
// its Const leaves. These stand in for the Foldable/Traversable typeclass
// objects the trees repository looks up; here they are plain stateless
// structs the algorithm objects inherit from.
// ---------------------------------------------------------------------

struct ExprFoldableMap {
    /** Number of Const leaves, counted in one fold. */
    constexpr auto length(const Expr& tree) const -> std::size_t {
        return fold_fix<std::size_t>(
            [](const ExprF<std::size_t>& layer) -> std::size_t {
                return std::visit(overloaded{
                                      [](const Const<std::size_t>&) -> std::size_t { return 1U; },
                                      [](const Add<std::size_t>& a) -> std::size_t { return *a.left + *a.right; },
                                      [](const Mul<std::size_t>& m) -> std::size_t { return *m.left + *m.right; },
                                  },
                                  layer);
            },
            tree);
    }
};

struct ExprTraversableMap {
    using element_type = int;

    /** Rebuild the tree elementwise inside optional: @p fn maps each leaf
     * value to optional<int>; one disengaged result collapses the whole
     * rebuild to nullopt (effectful traversal, optional Applicative). */
    template <class Fn>
    constexpr auto for_each(const Expr& tree, Fn&& fn) const -> std::optional<Expr> {
        using R = std::optional<Expr>;
        return fold_fix<R>(
            [&fn](const ExprF<R>& layer) -> R {
                return std::visit(overloaded{
                                      [&fn](const Const<R>& c) -> R {
                                          auto mapped = std::invoke(fn, c.val);
                                          if (!mapped)
                                              return std::nullopt;
                                          return const_node(*mapped);
                                      },
                                      [](const Add<R>& a) -> R {
                                          if (!*a.left || !*a.right)
                                              return std::nullopt;
                                          return add_node(**a.left, **a.right);
                                      },
                                      [](const Mul<R>& m) -> R {
                                          if (!*m.left || !*m.right)
                                              return std::nullopt;
                                          return mul_node(**m.left, **m.right);
                                      },
                                  },
                                  layer);
            },
            tree);
    }
};

// Example-local lookup, mirroring the shape of functor_typeclass: a
// variable template maps a type to its typeclass object.
template <class T>
inline constexpr auto example_foldable_typeclass = std::false_type{};
template <>
inline constexpr auto example_foldable_typeclass<Expr> = ExprFoldableMap{};

template <class T>
inline constexpr auto example_traversable_typeclass = std::false_type{};
template <>
inline constexpr auto example_traversable_typeclass<Expr> = ExprTraversableMap{};
// a66c0122-f24d-4858-a802-cf76f87969e9 end

namespace detail {

// 868a8228-44a7-4761-8822-28b622d6b9a0
// validate_impl: inherits the looked-up Traversable map for T (a stateless
// empty struct), so its operations are available as unqualified this->
// member calls. Ported from smd::tree::algorithm::detail::validate_impl.
template <class T, const auto& TC = example_traversable_typeclass<std::remove_cvref_t<T>>>
struct validate_impl : std::remove_cvref_t<decltype(TC)> {
    template <class Pred>
    constexpr auto call(Pred&& pred, const T& value) const {
        using element_type = typename std::remove_cvref_t<decltype(TC)>::element_type;
        return this->for_each(value, [&](const element_type& elem) -> std::optional<element_type> {
            if (pred(elem))
                return {elem};
            return std::nullopt;
        });
    }
};
// 868a8228-44a7-4761-8822-28b622d6b9a0 end

// 7226de56-d7ce-44c6-9e99-849da548d3e4
// transform_if_large_impl: inherits both the Foldable and the Traversable
// map for T. Multi-typeclass composition — Foldable gives length,
// Traversable gives for_each. Both bases are empty. Ported from
// smd::tree::algorithm::detail::transform_if_large_impl.
template <class T,
          const auto& FC = example_foldable_typeclass<std::remove_cvref_t<T>>,
          const auto& TC = example_traversable_typeclass<std::remove_cvref_t<T>>>
struct transform_if_large_impl : std::remove_cvref_t<decltype(FC)>, std::remove_cvref_t<decltype(TC)> {
    using foldable_base    = std::remove_cvref_t<decltype(FC)>;
    using traversable_base = std::remove_cvref_t<decltype(TC)>;
    using element_type     = typename traversable_base::element_type;

    template <class F>
    constexpr auto call(std::size_t min_size, F&& f, const T& value) const {
        auto n = this->foldable_base::length(value);
        if (n < min_size)
            return std::optional<T>{};

        return this->traversable_base::for_each(
            value, [&](const element_type& elem) -> std::optional<element_type> { return {f(elem)}; });
    }
};
// 7226de56-d7ce-44c6-9e99-849da548d3e4 end

} // namespace detail

// validate: returns optional<Tree>; nullopt if any element fails the
// predicate. Caller writes: example::validate(pred, tree).
struct validate_fn {
    template <class Pred, class T>
    constexpr auto operator()(Pred&& pred, T&& value) const {
        return detail::validate_impl<std::remove_cvref_t<T>>{}.call(std::forward<Pred>(pred), std::forward<T>(value));
    }
};
inline constexpr validate_fn validate{};

// transform_if_large: applies f to every element only if the tree has at
// least min_size elements. Returns optional<Tree>.
struct transform_if_large_fn {
    template <class F, class T>
    constexpr auto operator()(std::size_t min_size, F&& f, T&& value) const {
        return detail::transform_if_large_impl<std::remove_cvref_t<T>>{}.call(
            min_size, std::forward<F>(f), std::forward<T>(value));
    }
};
inline constexpr transform_if_large_fn transform_if_large{};

// DEV-04 spirit: the whole pipeline — build, validate, transform, tear
// down — works in constant evaluation.
static_assert([] {
    auto tree = mul_node(add_node(const_node(1), const_node(2)), const_node(3));
    return validate([](int x) { return x > 0; }, tree).has_value();
}());

static_assert([] {
    auto tree = mul_node(add_node(const_node(-1), const_node(2)), const_node(3));
    return !validate([](int x) { return x > 0; }, tree).has_value();
}());

static_assert([] {
    auto tree        = add_node(const_node(1), const_node(2));
    auto transformed = transform_if_large(2, [](int x) { return x + 10; }, tree);
    return transformed.has_value() && eval(*transformed) == 23;
}());

} // namespace example

int main() {
    using example::add_node;
    using example::const_node;
    using example::eval;
    using example::mul_node;

    bool ok = true;

    // (1 + 2) * 3 — all leaves positive: validate returns the rebuilt tree.
    auto tree = mul_node(add_node(const_node(1), const_node(2)), const_node(3));

    auto valid = example::validate([](int x) { return x > 0; }, tree);
    std::println("validate(x > 0, (1 + 2) * 3) engaged: {}", valid.has_value());
    ok = ok && valid.has_value() && eval(*valid) == 9;

    // One negative leaf poisons the whole traversal.
    auto tainted  = mul_node(add_node(const_node(-1), const_node(2)), const_node(3));
    auto rejected = example::validate([](int x) { return x > 0; }, tainted);
    std::println("validate(x > 0, (-1 + 2) * 3) engaged: {}", rejected.has_value());
    ok = ok && !rejected.has_value();

    // Four leaves >= 3: transform runs, every leaf scaled by 10.
    auto big = mul_node(add_node(const_node(1), const_node(2)), add_node(const_node(3), const_node(4)));

    auto scaled = example::transform_if_large(3, [](int x) { return x * 10; }, big);
    std::println("transform_if_large(3, x * 10, (1 + 2) * (3 + 4)) = {}",
                 scaled ? eval(*scaled) : -1); // 30 * 70 = 2100
    ok = ok && scaled.has_value() && eval(*scaled) == 2100;

    // Two leaves < 5: too small, nothing happens.
    auto small   = add_node(const_node(1), const_node(2));
    auto skipped = example::transform_if_large(5, [](int x) { return x * 10; }, small);
    std::println("transform_if_large(5, x * 10, (1 + 2)) engaged: {}", skipped.has_value());
    ok = ok && !skipped.has_value();

    return ok ? 0 : 1;
}
