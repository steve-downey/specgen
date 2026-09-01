// tests/corpus/spec_aliases.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_ALIASES_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_ALIASES_HPP

namespace demo {
namespace detail {
template <class T>
using iterator = T*;
using token    = int;
} // namespace detail

template <class T>
class range {
  public:
    // \ref{range.types}, member types
    using unmarked_type = T;

    //! \remarks The iterator type is implementation-defined.
    //! \impdef
    //! \at range.types
    using iterator = detail::iterator<T>;

    //! \remarks The aliases name the range's value types.
    //! \at range.types
    using value_type = T;

    //! \also
    //! \at range.types
    using const_value_type = const T;

    //! \remarks The token type is specified below.
    //! \seebelow
    //! \at range.types
    using token_type = const detail::token;
};

// \rSec3[range.types]{Member types}
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_ALIASES_HPP
