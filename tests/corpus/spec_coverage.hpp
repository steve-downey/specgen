// tests/corpus/spec_coverage.hpp                                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_COVERAGE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_COVERAGE_HPP

namespace demo {
// The class synopsis retains its requires-clause, so make this qualifier visible.
//! \specified
namespace detail {
template <class>
inline constexpr bool is_in_place_type = false;
}

template <class T>
using remove_cvref_t = T;

// \rSec2[demo.coverage]{Explicit coverage}

//! \constraints
//! \covers !detail::is_in_place_type<remove_cvref_t<F>>
//! \item `remove_cvref_t<F>` is not a specialization of `in_place_type_t`.
template <class F>
    requires(!detail::is_in_place_type<remove_cvref_t<F>>)
void accept(F) {}

template <class T>
using decay_t = T;
template <class T, class U>
inline constexpr bool is_same_v = __is_same(T, U);
template <class T>
struct in_place_type_t {};

// \rSec3[demo.coverage.class]{Class move_only_function}

//! The wrapper stores a callable object.
class move_only_function {
  private:
    // \ref{demo.coverage.ctors}, exposition members

    //! \expos(is-callable-from)
    //! \seebelow
    //! The predicate determines whether `VT` can be invoked.
    template <class VT>
    static constexpr bool is_callable_from = true;

  public:
    // \ref{demo.coverage.ctors}, constructors

    //! Let `VT` be `decay_t<F>`.
    //! \constraints
    //! \covers !detail::is_in_place_type<remove_cvref_t<F>>
    //! \item `remove_cvref_t<F>` is not the same type as `move_only_function`, and
    //! \item `remove_cvref_t<F>` is not a specialization of `in_place_type_t`, and
    //! \item `is_callable_from<VT>` is `true`.
    template <class F>
        requires(!is_same_v<remove_cvref_t<F>, move_only_function>) &&
                (!detail::is_in_place_type<remove_cvref_t<F>>) && (is_callable_from<decay_t<F>>)
    constexpr move_only_function(F&& f) {}

    //! Let `VT` be `decay_t<T>`.
    //! \mandates `VT` is the same type as `T`.
    template <class T>
    constexpr explicit move_only_function(in_place_type_t<T>) {
        static_assert(is_same_v<decay_t<T>, T>);
    }
};

// \rSec3[demo.coverage.ctors]{Construction}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_COVERAGE_HPP
