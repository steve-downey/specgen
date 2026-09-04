// tests/corpus/spec_class_description.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A class or class-template *definition*'s own docblock (design §6, issue
// #18). A definition heads a synopsis, so what the author writes about the
// *type* has to travel beside that synopsis rather than as an item of its
// own; the four shapes here are the ones that used to drop it silently.
//
//  - `tag` — a plain record definition. Its `\remarks` is the whole of its
//    wording: no members, no derivation, nothing else to hide the loss.
//  - `box` — a class template with a routed member. The synopsis, the
//    `\rSec3` frame and the member's own item all rendered correctly before;
//    only the class's own `\remarks` was missing, which is why this shape is
//    here rather than folded into `tag`.
//  - `checked` — a class template whose direct `static_assert` derives the
//    class-scope Mandates paragraph of design §5.2. The authored `\mandates`
//    *replaces* that paragraph and inherits its conjuncts as validator-only
//    drift evidence, exactly as an authored member Mandates does. The
//    authored wording names no derived subject, so the drift check stays
//    silent and `golden.class_description.validate` stays clean per corpus
//    convention.
//  - `probe` — the same derivation with *no* authored Mandates, so the
//    derived paragraph still lands and the class's `\remarks` follows it.
//    The pair of these two is the whole replacement rule.
//
// Self-contained (no #includes): `acceptable_v` is a stub variable template,
// so the assertions resolve standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CLASS_DESCRIPTION_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CLASS_DESCRIPTION_HPP

namespace demo {

template <class T>
constexpr bool acceptable_v = true;

//! \remarks A defined record's own description.
struct tag {};

//! \mandates `T` is a type this facility accepts.
//! \remarks Instantiating `checked` registers nothing; it only constrains.
template <class T>
struct checked {
    static_assert(acceptable_v<T>, "T must be acceptable");
};

//! \remarks The derived paragraph above states the requirement; this says
//! what the type is for.
template <class T>
struct probe {
    static_assert(acceptable_v<T>);
};

// `box` comes last because the \rSec3 its member routes to stays open to the
// end of the header: anything declared after that marker would land inside
// [box.mem] rather than beside its own synopsis.

//! \remarks A `box` is a distinct type for each `T`; two `box` types with
//! different `T` never compare equal.
template <class T>
struct box {
    // \ref{box.mem}, member types
    //! \remarks The contained type.
    using type = T;
};

// \rSec3[box.mem]{Member types}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CLASS_DESCRIPTION_HPP
