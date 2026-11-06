// tests/corpus/spec_expos_static_items.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_ITEMS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_ITEMS_HPP

namespace demo {

class predicate {
  public:
    // \ref{predicate.members}, exposition members
    //! \expos(test-value)
    //! \remarks `value_` is always true.
    static constexpr bool value_ = true;

  private:
    //! \expos(accepts-type)
    //! \seebelow
    //! `accepts_<T>` tests whether the type can be called.
    //! \example For an ordinary type:
    //! \codeblock
    //! static_assert(true);
    //! \endcodeblock
    //! \endexample
    template <class T>
    static constexpr bool accepts_ = sizeof(T) > 0;

    //! \expos(storage-limit)
    //! \at predicate.limits
    //! \remarks The limit used by `limit_`.
    static constexpr int limit_{42};

    //! \expos
    static constexpr int marker_only_ = 8;
};

// \rSec2[predicate]{Class predicate}
// \rSec3[predicate.members]{Exposition members}
// \rSec3[predicate.limits]{Limits}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_STATIC_ITEMS_HPP
