// tests/corpus/spec_reqexpr.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A requires-expression in a template-head constraint is an ordinary
// constraint spelling: it must not change whether a documented definition is
// recognized as one (issue #20). Clang anchors doc-comment attachment for a
// templated function at its *name* and rejects any comment separated from it
// by `;{}#@` — all of which `requires requires(T t) { ... }` places between
// the template header and the name — so the lookup goes through the described
// FunctionTemplateDecl instead. Each requires-expression form (in-class
// member, hidden friend, free function) has a named-concept twin that always
// worked; the pairs must render identically apart from the constraint itself.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_REQEXPR_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_REQEXPR_HPP

namespace demo {

template <class T>
concept nonzero = sizeof(T) > 0;

// \rSec3[demo.box]{Class `box`}

struct box {
    // \ref{demo.box}, observers

    //! \returns `0`.
    template <class T>
        requires requires(T t) {
            { t == 0 };
        }
    constexpr int with_req_expr(T) const {
        return 0;
    }

    //! \returns `1`.
    template <class T>
        requires nonzero<T>
    constexpr int with_named(T) const {
        return 1;
    }

    //! \returns Whether `it` is at the end.
    template <class I>
        requires requires(I i) {
            { *i == 0 };
        }
    friend constexpr bool operator==(const I& it, box) {
        return *it == 0;
    }
};

// \rSec3[demo.free]{Free functions}

//! \returns `2`.
template <class T>
    requires requires(T t) {
        { t == 0 };
    }
constexpr int free_with_req_expr(T) {
    return 2;
}

//! \returns `3`.
template <class T>
    requires nonzero<T>
constexpr int free_with_named(T) {
    return 3;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_REQEXPR_HPP
