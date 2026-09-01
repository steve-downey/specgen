// tests/corpus/spec_imported_qualifier.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_IMPORTED_QUALIFIER_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_IMPORTED_QUALIFIER_HPP

namespace std {
template <class T>
inline constexpr bool imported_trait_v = true;
} // namespace std

namespace demo {
namespace detail {
using std::imported_trait_v;

template <class T>
inline constexpr bool local_trait_v = true;
} // namespace detail

namespace imported_detail {
using std::imported_trait_v;
} // namespace imported_detail

class probe {
  public:
    // \ref{probe.obs}, observers
    //! \effects-equiv
    template <class T>
    bool check() const {
        return detail::imported_trait_v<T> && imported_detail::imported_trait_v<T> && detail::local_trait_v<T>;
    }
};

// \rSec3[probe.obs]{Observers}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_IMPORTED_QUALIFIER_HPP
