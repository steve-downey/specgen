// tests/corpus/spec_callable_wrapper.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CALLABLE_WRAPPER_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CALLABLE_WRAPPER_HPP

namespace demo {

// \rSec2[callable]{Callable wrappers}
// \rSec3[callable.general]{General}

//! The header provides callable wrappers for each combination of the placeholders.
//!
//! The invocation qualifiers determine how the stored target is called.

// \rSec3[callable.class]{Class template callable_wrapper}

//! The `callable_wrapper` class template provides polymorphic wrappers that
//! generalize the notion of a callable object\iref{func.def}.
//! These wrappers can store, move, and call arbitrary callable objects,
//! given a call signature.
//! \recommended Implementations should avoid the use of dynamically allocated
//! memory for a small contained value.
//! \note Such small-object optimization can only be applied to a type `T`
//! for which `is_nothrow_move_constructible_v<T>` is `true`.
//! \endnote
template <class R, class... ArgTypes>
class callable_wrapper {
  public:
    // \ref{callable.ctor}, constructors

    //! \ensures `*this` has no target object if any of the following hold:
    //! \item `f` is a null function pointer value, or
    //! \item `f` is a null member pointer value, or
    //! \item `f` is an empty callable wrapper.
    //!
    //! Otherwise, `*this` has a target object initialized with `f`.
    template <class F>
    callable_wrapper(F&& f) {}

  private:
    // \ref{callable.ctor}, exposition members

    //! \expos(is-callable-from)
    //! \seebelow
    //! If the invocation is nonthrowing, `is_callable_from<VT>` is equal to:
    //! \codeblock
    //! is_nothrow_invocable_r_v<R, VT, ArgTypes...>
    //! \endcodeblock
    //! Otherwise, `is_callable_from<VT>` is equal to:
    //! \codeblock
    //! is_invocable_r_v<R, VT, ArgTypes...>
    //! \endcodeblock
    //! \example A callable target can be checked as follows:
    //! \codeblock
    //! static_assert(is_invocable_r_v<int, int (*)()>);
    //! \endcodeblock
    //! \endexample
    template <class VT>
    static constexpr bool is_callable_from = true;
};

// \rSec3[callable.ctor]{Construction}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CALLABLE_WRAPPER_HPP
