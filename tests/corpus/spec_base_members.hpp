// tests/corpus/spec_base_members.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): a class whose
// synopsis declares members it has from base classes (issue #342), the shape
// of an owning callable wrapper whose call operator comes from a base that is
// partially specialized once per cv-qualifier.
//
// `wrapper<Sig>` derives from `call_base<wrapper<Sig>, Sig>`, which depends
// on its parameter, so the call operator is whichever of call_base's
// specializations applies: the primary is declared and not defined, and each
// partial specialization declares one `constexpr R operator()`. It also
// derives from `observers`, an ordinary class, for `empty()` and `size()`.
// Its `\verbatim-synopsis` declares all of them, and the check compares each
// line with the base declaration of the same name:
//
// - `operator()` agrees with every specialization;
// - `empty()` agrees with `observers`;
// - `size()` is `constexpr` in `observers` and not in the synopsis: a warning;
// - `reset()` is declared by no base and not by the class: the note.
//
// Self-contained and parsed standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_BASE_MEMBERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_BASE_MEMBERS_HPP

namespace demo {

namespace detail {

template <class Derived, class Sig>
class call_base; // not defined

//! \omit
template <class Derived, class R, class... Args>
class call_base<Derived, R(Args...)> {
  public:
    constexpr R operator()(Args... args) { return static_cast<Derived&>(*this).call(args...); }

  private:
    constexpr int private_helper() const { return 0; }
};

//! \omit
template <class Derived, class R, class... Args>
class call_base<Derived, R(Args...) const> {
  public:
    constexpr R operator()(Args... args) const { return static_cast<const Derived&>(*this).call(args...); }
};

//! \omit
struct observers {
    constexpr bool     empty() const noexcept { return true; }
    constexpr unsigned size() const noexcept { return 0; }
    observers&         operator=(const observers&) = default;
};

} // namespace detail

// \rSec2[demo.wrapper]{Class template \tcode{wrapper}}

//! The wrapper.
//! \verbatim-synopsis
//! template<class R, class... ArgTypes>
//! class wrapper<R(ArgTypes...) @\cv{}@> {
//! public:
//!   constexpr wrapper() noexcept;
//!   constexpr R operator()(ArgTypes...) @\cv{}@;
//!   constexpr bool empty() const noexcept;
//!   unsigned size() const noexcept;
//!   void reset();
//! };
template <class Sig>
class wrapper : public detail::call_base<wrapper<Sig>, Sig>, public detail::observers {
  public:
    //! \effects Constructs an empty wrapper.
    //! \at demo.wrapper
    constexpr wrapper() noexcept {}

    //! \omit
    template <class... Args>
    constexpr int call(Args...) const {
        return 0;
    }
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_BASE_MEMBERS_HPP
