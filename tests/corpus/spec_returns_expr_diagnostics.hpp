// tests/corpus/spec_returns_expr_diagnostics.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The bodies `\returns-expr` refuses (design §4.2, issue #143). A *Returns*
// names a value and no effects, so a body that does anything besides return
// one expression needs `\effects-equiv`, whose Equivalent to: block says all
// of it:
//
// - `twice` computes a local first;
// - `checked` keeps a local alias, which is retained source as it is for
//   `\effects-equiv`;
// - `reset` returns no value.
//
// `legacy` uses the marker's old name, `\returns-equiv`, which is an Error
// naming the new one; `both` authors a Returns beside the marker.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_RETURNS_EXPR_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_RETURNS_EXPR_DIAGNOSTICS_HPP

namespace demo {

//! \returns-expr
inline int twice(int x) {
    int y = x + x;
    return y;
}

//! \returns-expr
template <class T>
int checked(T x) {
    using type = T;
    static_assert(sizeof(type) > 0);
    return static_cast<int>(x);
}

//! \returns-expr
inline void reset(int& x) {
    x = 0;
    return;
}

//! \returns-equiv
inline int legacy(int x) { return x; }

//! \returns-expr
//! \returns `x`.
inline int both(int x) { return x; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_RETURNS_EXPR_DIAGNOSTICS_HPP
