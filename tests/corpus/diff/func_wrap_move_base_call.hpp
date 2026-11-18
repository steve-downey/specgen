// tests/corpus/diff/func_wrap_move_base_call.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): func_wrap_move_asdraft.hpp
// with the one difference that makes it the shape of the real implementation
// (issue #329). `operator()` is not a member the class declares: it comes
// from a base class, which is how one partial specialization stands for the
// draft's twelve, and its item is a detached `\verbatim-itemdecl` under
// [func.wrap.move.inv], where the paper makes it `constexpr`.
//
// The class's own declarations are what `\asdraft-synopsis` patches the
// draft's synopsis from, and `operator()` is not among them. Its line takes
// `constexpr` from the item that declares it, so the synopsis and the item
// agree and the diff marks the specifier in both. The synopsis check reads
// the base class's declaration of it, which agrees, and says nothing.
//
// Self-contained and parsed standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_FUNC_WRAP_MOVE_BASE_CALL_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_FUNC_WRAP_MOVE_BASE_CALL_HPP

namespace demo {

using nullptr_t = decltype(nullptr);
template <class T>
struct in_place_type_t {};
template <class T>
struct initializer_list {};

template <class... S>
class move_only_function; // not defined

namespace detail {
//! \omit
template <class R, class... ArgTypes>
struct call_base {
    constexpr R operator()(ArgTypes... args) {
        (void)sizeof...(args);
        return R();
    }
};
} // namespace detail

// \rSec3[func.wrap.move]{Move-only wrapper}

// \rSec4[func.wrap.move.general]{General}

//! \asdraft

// \rSec4[func.wrap.move.class]{Class template \tcode{move_only_function}}

//! \asdraft
//! \asdraft-synopsis
template <class R, class... ArgTypes>
class move_only_function<R(ArgTypes...)> : public detail::call_base<R, ArgTypes...> {
    // \ref{func.wrap.move.ctor}, constructors, assignments, and destructor

    // Exposition-only, so the draft's index does not name it and its item
    // there has no names to pair by: it pairs by how its declaration reads.
    //! \asdraft
    //! \expos
    //! \seebelow
    template <class VT>
    static constexpr bool is_callable_from = true;

  public:
    using result_type = R;

    //! \asdraft
    constexpr move_only_function() noexcept {}
    //! \also
    constexpr move_only_function(nullptr_t) noexcept {}

    //! \asdraft
    constexpr move_only_function(move_only_function&& f) noexcept { (void)f; }

    //! \asdraft
    template <class F>
    constexpr move_only_function(F&& f) {
        (void)f;
    }

    //! \asdraft
    template <class T, class... Args>
    constexpr explicit move_only_function(in_place_type_t<T>, Args&&... args) {
        (void)sizeof...(args);
    }

    //! \asdraft
    template <class T, class U, class... Args>
    constexpr explicit move_only_function(in_place_type_t<T>, initializer_list<U> ilist, Args&&... args) {
        (void)ilist;
        (void)sizeof...(args);
    }

    //! \asdraft
    constexpr move_only_function& operator=(move_only_function&& f) {
        (void)f;
        return *this;
    }

    //! \asdraft
    constexpr move_only_function& operator=(nullptr_t) noexcept { return *this; }

    //! \asdraft
    template <class F>
    constexpr move_only_function& operator=(F&& f) {
        (void)f;
        return *this;
    }

    //! \asdraft
    constexpr ~move_only_function() {}

    // \ref{func.wrap.move.inv}, invocation

    //! \asdraft
    constexpr explicit operator bool() const noexcept { return false; }

    // \ref{func.wrap.move.util}, utility

    //! \asdraft
    constexpr void swap(move_only_function& other) noexcept { (void)other; }

    //! \asdraft
    friend constexpr void swap(move_only_function& f1, move_only_function& f2) noexcept {
        (void)f1;
        (void)f2;
    }

    //! \asdraft
    friend constexpr bool operator==(const move_only_function& f, nullptr_t) noexcept { return (void)f, true; }
};

// \rSec4[func.wrap.move.ctor]{Constructors, assignments, and destructor}

// \rSec4[func.wrap.move.inv]{Invocation}

//! \asdraft
//! \verbatim-itemdecl
//! constexpr R operator()(ArgTypes... args) @\cv{}@ @\placeholder{ref}@ noexcept(@\placeholder{noex}@);

// \rSec4[func.wrap.move.util]{Utility}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_FUNC_WRAP_MOVE_BASE_CALL_HPP
