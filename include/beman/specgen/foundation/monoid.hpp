// include/beman/specgen/foundation/monoid.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The explicit monoid spelling. A monoid is nothing
// more than the (combine, identity) pair the vendored fold_map already takes as
// two explicit parameters (vendor/tree_algorithms/.../fold_map.hpp): this header
// bundles that pair into one value so it can be passed around and named — the
// validator's Diagnostics monoid (decision expected-error-taxonomy) is the
// motivating instance. No typeclass lookup and no CRTP base: the
// explicit-parameter tier only (decision no-typeclass-objects).
#ifndef BEMAN_SPECGEN_FOUNDATION_MONOID_HPP
#define BEMAN_SPECGEN_FOUNDATION_MONOID_HPP

#include <ranges>
#include <utility>

namespace beman::specgen::foundation {

/// A monoid over @p T: an associative binary @c combine and its @c identity.
///
/// The two laws every instance must satisfy, and which the tests check:
///   - associativity: combine(a, combine(b, c)) == combine(combine(a, b), c)
///   - identity:      combine(identity, a) == a == combine(a, identity)
///
/// @tparam T       the carrier type.
/// @tparam Combine callable (T, T) -> T, associative.
template <typename T, typename Combine>
struct monoid {
    Combine combine;
    T       identity;
};

// CTAD: monoid{f, id} deduces monoid<decltype(id), decltype(f)>. The combine
// argument comes first so it reads in operator order, but T is the identity's
// type, so the guide reorders the template parameters accordingly.
template <typename Combine, typename T>
monoid(Combine, T) -> monoid<T, Combine>;

/// Fold @p range left-to-right under @p m, starting from its identity.
/// mconcat({}, m) == m.identity, so an empty range is well defined.
template <std::ranges::input_range Range, typename T, typename Combine>
constexpr T mconcat(const Range& range, const monoid<T, Combine>& m) {
    T acc = m.identity;
    for (auto&& element : range) { // substrate generic algorithm
        acc = m.combine(std::move(acc), element);
    }
    return acc;
}

/// Fold @p range under @p m after mapping each element through @p map_fn — the
/// fold-map shape (map elements into the carrier, then combine). This is the
/// two-parameter (map_fn, monoid) spelling of the vendored fold_map's
/// (map_fn, combine, identity) primary form.
template <std::ranges::input_range Range, typename MapFn, typename T, typename Combine>
constexpr T mconcat_map(const Range& range, const MapFn& map_fn, const monoid<T, Combine>& m) {
    T acc = m.identity;
    for (auto&& element : range) { // substrate generic algorithm
        acc = m.combine(std::move(acc), map_fn(element));
    }
    return acc;
}

} // namespace beman::specgen::foundation

#endif // BEMAN_SPECGEN_FOUNDATION_MONOID_HPP
