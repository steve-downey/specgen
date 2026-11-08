// tests/corpus/spec_drift_prose.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DRIFT_PROSE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DRIFT_PROSE_HPP

namespace demo {

template <class T, class U>
inline constexpr bool is_same_v = __is_same(T, U);
template <class T>
using decay_t = T;
template <class>
inline constexpr bool callable = true;

// \rSec3[drift.functions]{Functions}

//! Let `VT` be `F`.
//! \constraints `VT` is not the same type as `int`.
template <class F>
    requires(!is_same_v<F, int>)
void accept(F) {}

//! Let `VT` be `decay_t<T>`.
//! \mandates `VT` is the same type as `T`.
template <class T>
void construct() {
    static_assert(is_same_v<decay_t<T>, T>);
}

//! Let `VT` be `decay_t<F>`.
//! \constraints `callable<VT>` is `true`.
template <class F>
    requires(callable<decay_t<F>>)
void invoke(F) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DRIFT_PROSE_HPP
