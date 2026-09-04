// tests/corpus/spec_verbatim_attached.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A verbatim docblock written as a declaration's own docblock (no blank
// line) *replaces* that declaration's extracted form — one node, the
// authored one. `\verbatim-synopsis` on the class keeps its members, roster,
// and routing intact while the synopsis text is the author's;
// `\verbatim-itemdecl` on the function masks the implementation spelling
// (`detail::ctx_t<int>`) the marker exists to keep out of wording. A
// detached block still stands alone (spec_verbatim.hpp,
// spec_verbatim_record_merge.hpp).

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_ATTACHED_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_ATTACHED_HPP

namespace demo {
namespace detail {
template <class T>
using ctx_t = T;
} // namespace detail

//! \verbatim-synopsis
//! template<class T>
//! struct widget {
//!   auto probe(int x) const;
//! };
template <class T>
struct widget {
    // \ref{demo.obs}, observers
    auto probe(int x) const;
};

// \rSec3[demo.obs]{Observers}

//! \returns Something.
template <class T>
auto widget<T>::probe(int x) const {
    return x;
}

//! \returns Something masked.
//! \verbatim-itemdecl
//! template<class F>
//! auto go(F&& f, see below x);
template <class F>
auto go(F&& f, detail::ctx_t<int> x) {
    return f(x);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_ATTACHED_HPP
