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

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_USES_HPP
