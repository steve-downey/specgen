// tests/corpus/spec_asdraft_expos_diagnostics.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\asdraft` on an exposition-only declaration that has no item (issue #328).
// An exposition-only member variable has one, and inherits as any other member
// does (tests/corpus/diff/func_wrap_move_asdraft.hpp). An exposition-only
// declaration at namespace scope is a synopsis with its description written
// beside it, and an exposition-only alias in a class is a line of the class
// synopsis: neither is an item the baseline's could pair with, so the request
// is an Error on each and not wording that quietly lacks its description.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_ASDRAFT_EXPOS_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_ASDRAFT_EXPOS_DIAGNOSTICS_HPP

namespace demo {

// \rSec2[demo.expos]{Exposition-only declarations}

//! \asdraft
//! \expos
template <class T>
concept thing_like = true;

//! \asdraft
//! \expos
//! \seebelow
template <class T>
inline constexpr bool is_thing = true;

//! Holds a thing.
class holder {
    //! \asdraft
    //! \expos
    using stored = int;

  public:
    //! \effects Nothing.
    void clear() {}
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_ASDRAFT_EXPOS_DIAGNOSTICS_HPP
