// tests/corpus/diff/func_wrap_move_asdraft.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): the shape of a paper
// whose whole change to [func.wrap.move] is `constexpr`, which is P3574's
// (docs/plans/draft-import.md). Nothing of the draft's wording is written
// here: the section's general prose, the class's own description and its
// synopsis, and every member's description are the draft's, taken from
// tests/corpus/standard-slices/func.wrap.move.slice with `\asdraft` and
// `\asdraft-synopsis`. What the header says is the declarations, which the
// synopsis is patched from, so the diff against the draft is `constexpr` and
// nothing else.
//
// The draft's partial specialization has placeholders for the cv-qualifier,
// the ref-qualifier and `noexcept`; this one is a single specialization with
// none, so the declarations that differ only there read the way they do in
// the draft's `\cv{}` and `noexcept(noex)` positions.
//
// Self-contained and parsed standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_FUNC_WRAP_MOVE_ASDRAFT_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_FUNC_WRAP_MOVE_ASDRAFT_HPP

namespace demo {

using nullptr_t = decltype(nullptr);
template <class T>
struct in_place_type_t {};
template <class T>
struct initializer_list {};

template <class... S>
class move_only_function; // not defined

// \rSec3[func.wrap.move]{Move-only wrapper}

// \rSec4[func.wrap.move.general]{General}

//! \asdraft

// \rSec4[func.wrap.move.class]{Class template \tcode{move_only_function}}

//! \asdraft
//! \asdraft-synopsis
template <class R, class... ArgTypes>
class move_only_function<R(ArgTypes...)> {
  public:
    using result_type = R;

    // \ref{func.wrap.move.ctor}, constructors, assignments, and destructor

    //! \asdraft
    constexpr move_only_function() noexcept {}
    //! \also
    constexpr move_only_function(nullptr_t) noexcept {}

    //! \asdraft
    constexpr move_only_function(move_only_function&& f) noexcept { (void)f; }

    //! \asdraft
    template <class F>
    constexpr move_only_function(F&& f) { (void)f; }

    //! \asdraft
    template <class T, class... Args>
    constexpr explicit move_only_function(in_place_type_t<T>, Args&&... args) { (void)sizeof...(args); }

    //! \asdraft
    template <class T, class U, class... Args>
    constexpr explicit move_only_function(in_place_type_t<T>, initializer_list<U> ilist, Args&&... args) { (void)ilist; (void)sizeof...(args); }

    //! \asdraft
    constexpr move_only_function& operator=(move_only_function&& f) { (void)f; return *this; }

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

    //! \asdraft
    constexpr R operator()(ArgTypes... args) { (void)sizeof...(args); return R(); }

    // \ref{func.wrap.move.util}, utility

    //! \asdraft
    constexpr void swap(move_only_function& other) noexcept { (void)other; }

    //! \asdraft
    friend constexpr void swap(move_only_function& f1, move_only_function& f2) noexcept { (void)f1; (void)f2; }

    //! \asdraft
    friend constexpr bool operator==(const move_only_function& f, nullptr_t) noexcept { return (void)f, true; }

  private:
    //! \omit
    template <class VT>
    static constexpr bool is_callable_from = true;
};

// \rSec4[func.wrap.move.ctor]{Constructors, assignments, and destructor}

// \rSec4[func.wrap.move.inv]{Invocation}

// \rSec4[func.wrap.move.util]{Utility}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_FUNC_WRAP_MOVE_ASDRAFT_HPP
