// tests/corpus/spec_seebelow.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §4.3: `\seebelow`). Bare
// `\seebelow` replaces a return type; the `explicit` and `noexcept` targets
// replace only the corresponding conditional-specifier operand. The named
// cases are marked on out-of-line definitions and contain a droppable `demo::`
// qualifier, exercising both pre-pass target propagation and dominant nested
// edits. Self-contained (no #includes), so it parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SEEBELOW_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SEEBELOW_HPP

namespace demo {

template <class From, class To>
inline constexpr bool is_convertible_v = false;

template <class T>
inline constexpr bool is_nothrow_swappable_v = false;

template <class T>
class future {
  public:
    // \ref{future.cons}, constructors
    template <class U>
    explicit(!demo::is_convertible_v<U, T>) future(U&&);

    // \ref{future.swap}, swap
    void swap(future&) noexcept(demo::is_nothrow_swappable_v<T>);

    // \ref{future.monadic}, monadic operations
    template <class F>
    auto transform(F&& f) const;

  private:
    T value_;
};

// \rSec3[future.cons]{Constructors}

//! \seebelow explicit
//! \effects Constructs a future from the argument.
template <class T>
template <class U>
future<T>::future(U&&) {}

// \rSec3[future.swap]{Swap}

//! \seebelow noexcept
//! \effects Exchanges the states of the two futures.
template <class T>
void future<T>::swap(future&) noexcept(demo::is_nothrow_swappable_v<T>) {}

// \rSec3[future.monadic]{Monadic operations}

//! \seebelow
//! \effects Applies `f` to the contained value and wraps the result.
//! \remarks The return type is `future<invoke_result_t<F, T>>`.
template <class T>
template <class F>
auto future<T>::transform(F&& f) const {
    return f(value_);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SEEBELOW_HPP
