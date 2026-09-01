// include/beman/specgen/foundation/overloaded.hpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Provenance: ported from compile-time-scheme
//   repo:   https://github.com/steve-downey/compile-time-scheme (commit f60b0ff)
//   path:   src/smd/fixpoint/overloaded.hpp
// Adapted by renaming the namespace to beman::specgen::foundation, and by
// giving the catch-all a deduced return type (see DIVERGENCES OBS-5:
// upstream's hard-coded `void` loses the named diagnostic for every
// value-returning visitor). See foundation/DIVERGENCES.md for why this is
// ported rather than consumed from the vendored subtree
// (decision subtree-consumption).
#ifndef BEMAN_SPECGEN_FOUNDATION_OVERLOADED_HPP
#define BEMAN_SPECGEN_FOUNDATION_OVERLOADED_HPP

// overloaded<Ts...> — aggregate visitor for std::visit (decision visitation-rules).
//
// Usage:
//   std::visit(overloaded{
//       [](int x)         { ... },
//       [](std::string s) { ... },
//   }, v);
//
// The consteval catch-all fires a static_assert at compile time if std::visit
// encounters an alternative not covered by the explicit cases. This turns
// variant exhaustiveness into a hard compile error naming the omission rather
// than a silent default/no-op: adding a new alternative to a variant without
// handling it everywhere is caught immediately. This tripwire is the whole
// reason foundation carries its own overloaded (decision visitation-rules);
// the plain aggregate visitor is not enough.
//
// The catch-all's return type must be *deduced*, not a hard-coded `void`.
// std::visit checks that every alternative's call yields the same
// return type, and that check only needs each call's return *type* — so
// against a value-returning visitor a `void` catch-all mismatches before its
// body is ever instantiated, and libstdc++'s generic "same return type"
// assertion fires instead of the message below. Deducing the return type
// forces the body to be instantiated to compute it, which is what puts this
// static_assert first. Verified both ways by deleting a case (DIVERGENCES
// OBS-5).
//
// No explicit deduction guide is needed: C++20 CTAD for aggregates deduces
// overloaded<F1, F2, ...> from the constructor arguments.

namespace beman::specgen::foundation {

template <typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;

    consteval auto operator()(auto) const {
        static_assert(false, "overloaded: unhandled variant alternative — add a case");
    }
};

} // namespace beman::specgen::foundation

#endif // BEMAN_SPECGEN_FOUNDATION_OVERLOADED_HPP
