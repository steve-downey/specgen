// tests/corpus/spec_macro_continuations.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_MACRO_CONTINUATIONS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_MACRO_CONTINUATIONS_HPP

namespace demo {

// \rSec3[macro.general]{Macro continuations}

//! \placeholder{noex} is a placeholder in leading prose.

template <class T>
class gadget {
  public:
    //! \at macro.general
    //! \expects `T` meets the \oldconcept{Destructible} and
    //! \oldconcept{CopyConstructible} requirements.
    //! \returns `0`.
    //! \remarks The following descriptions apply:
    //! \item Meets
    //! \oldconcept{CopyConstructible} requirements.
    //!
    //! \placeholder{after} follows the list.
    //! \libtab2[macro.flat]{Flat table}
    //! \placeholder{caption}
    //! \column Type
    //! \cv{} qualifier
    //! \column Meaning
    //! \row A
    //! \placeholder{key}
    //! \cell Meets
    //! \oldconcept{Destructible} requirements.
    //! \endlibtab2
    //! \lib2dtab2[macro.grid]{Grid table}
    //! \column A
    //! \column B
    //! \row Type
    //! \cv{} qualifier
    //! \cell First
    //! \placeholder{value}
    //! \cell Second
    //! \placeholder{value}
    //! \endlib2dtab2
    //! \placeholder{resume} follows the tables.
    //! \note The type meets
    //! \oldconcept{Destructible} requirements.
    //! \endnote
    //! \example A
    //! \cv{} qualifier is written literally.
    //! \endexample
    int size() const { return 0; }
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_MACRO_CONTINUATIONS_HPP
