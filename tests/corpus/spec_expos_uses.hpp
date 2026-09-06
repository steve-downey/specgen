// tests/corpus/spec_expos_uses.hpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.5 / §4.1: exposition-only
// *uses*). `value_` is exposition-only, so besides rendering as `\exposid` in
// the synopsis it must also render that way wherever the class's own fragments
// mention it. Also resolved namespace-scope uses in an extracted body:
// an ordinary variable and a concept lose their `detail::` qualifier and gain
// exposid spans, while a same-named local variable remains ordinary code.
// Type uses resolve the same way: an exposition-only alias and alias template
// render as standalone synopses, the alias template's RHS naming the alias as
// an exposid, and a documented function's parameter written as
// `detail::traverse_context_t<int>` renders as `traverse-context-t<int>`.
// A class template's own head is the position the rewrite used to miss
// (issue #48): its requires-clause, its parameters' type constraints, and
// their default arguments belong to the ClassTemplateDecl rather than to the
// record, so a traversal rooted at the record never reached them -- and the
// same concept renamed correctly in a member's requires-clause kept its
// `detail::` spelling one line above, in the same rendered block.
// Self-contained (no #includes) under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_USES_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_USES_HPP

namespace demo {

namespace detail {

//! \expos(limit)
inline constexpr int exposed_limit = 1;

//! \expos(enabled)
template <class T>
concept enabled_for = true;

//! \expos
using token_ = int;

//! \expos
template <class T>
using traverse_context_t = token_*;

} // namespace detail

class counter {
  public:
    // \ref{counter.obs}, observers
    int  get() const;
    void bump();

  private:
    //! \expos
    int value_ = 0;
};

// \rSec3[counter.obs]{Observers}

//! \returns-equiv
int counter::get() const { return value_; }

//! \effects-equiv
//! \remarks Increments `value_` in place.
void counter::bump() {
    static_assert(detail::enabled_for<int>);
    int exposed_limit = 2;
    if constexpr (detail::enabled_for<int>)
        value_ = value_ + detail::exposed_limit + exposed_limit;
}

//! \returns The result of `f` applied to the dereferenced context.
template <class F>
int apply_in_context(F&& f, detail::traverse_context_t<int> context) {
    return f(*context);
}

// \rSec3[counter.gauge]{Class template `gauge`}

// All three head positions at once: a constrained parameter written in
// shorthand, a default argument, and a requires-clause.
template <detail::enabled_for U, class T = detail::token_>
    requires detail::enabled_for<T>
class gauge {
  public:
    // \ref{counter.gauge}, observers
    int level() const
        requires detail::enabled_for<U>;
};

//! \returns `0`.
template <detail::enabled_for U, class T>
    requires detail::enabled_for<T>
int gauge<U, T>::level() const
    requires detail::enabled_for<U>
{
    return 0;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_USES_HPP
