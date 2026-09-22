// tests/corpus/spec_deleted_derived_constraints.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A `= delete`d member's derived Constraints element does not, by itself,
// earn the member an itemdecl (issue #121). Before #120 a deleted member with
// a template-head requires-clause produced no derivation, so the in-class
// collection gate's `elements.empty()` correctly read "nothing to specify"
// for it. #120 taught `derive_constraints` to read a head clause too, which
// fills `elements` with a *derived* Constraints and, unfixed, opens the gate:
// the member gets an itemdecl whose only content is a restatement of its own
// declaration, on an overload the declaration says never participates.
//
// Three deleted overloads of `scratch`, same requires-clause, distinguished
// only by parameter type, so only what each docblock carries beyond the
// derivation differs:
//
//   * `scratch(G&&)` -- no docblock content beyond the bare sentinel --
//     carries nothing but the derived Constraints and must stay
//     synopsis-only, the pre-#120 (and post-fix) behavior.
//   * `scratch(const G&)` -- `\describe` forces an itemdecl despite carrying
//     no authored element, the same override `spec_inclass_markers.hpp` pins
//     for a `= default` member.
//   * `scratch(G)` -- an authored `\remarks` alongside the same derivation
//     earns an itemdecl the ordinary way; the gate must not have started
//     rejecting authored content too.
//
// Self-contained (no #includes) and built from a stub trait, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DELETED_DERIVED_CONSTRAINTS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DELETED_DERIVED_CONSTRAINTS_HPP

namespace demo {

template <class T>
constexpr bool is_int_v = false;
template <>
constexpr bool is_int_v<int> = true;

class scratch {
  public:
    // \ref{demo.scratch}, constructors

    //!
    template <class G>
        requires(!is_int_v<G>)
    constexpr scratch(G&&) = delete;

    //! \describe
    template <class G>
        requires(!is_int_v<G>)
    constexpr scratch(const G&) = delete;

    //! \remarks Rejects every type but `int`.
    template <class G>
        requires(!is_int_v<G>)
    constexpr scratch(G) = delete;
};

// \rSec3[demo.scratch]{Scratch}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DELETED_DERIVED_CONSTRAINTS_HPP
