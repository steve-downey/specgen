// tests/corpus/spec_gathered_class_at.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.4, §5.2, §6; issue #98): `\at` on a
// class *definition*'s own docblock routes the class's own wording — the
// class-scope `static_assert` paragraph and the description its docblock
// carries — to the section it names, instead of leaving both beside the
// synopsis. The marker used to be a silent no-op there, which is what stopped
// a multi-header library from folding its components into one gathered `.syn`
// region (issue #77) and moving each followed class's `\rSec` markers into the
// umbrella: the classes' own descriptions all stayed in the header-synopsis
// section.
//
// Six classes, one region and one class outside it:
//
//   - `crate`, folded in, routing both halves at once — the derived paragraph
//     and the authored `\remarks` land in [box.crate], in that order, and
//     [box.syn] carries neither,
//   - `pallet`, a folded-in class *template* with a `static_assert` and no
//     description of its own: the derived paragraph routes alone (and the
//     ClassTemplateDecl arm of classify() is the one reading the marker),
//   - `label`, folded in with a description and no assertion: the other half
//     routes alone,
//   - `crumb`, folded in and carrying both halves with *no* `\at`: unchanged,
//     still beside its synopsis in [box.syn], which is what issue #41 gave a
//     folded-in class and what must not regress,
//   - `strap`, folded in with an `\at` naming a section no `\rSec` opens:
//     `document_build::build_tree` drops the wording, and the `Routed` roster
//     entry the route earns is the only record that the request was made, so
//     design §9's dangling-route rule reports it instead of the silence a
//     misspelled marker used to buy. This header keeps that finding on
//     purpose; `gathered_class_at_validate` pins it, the spec_namespace.hpp
//     pattern,
//   - `hinge`, written *outside* any region under [box.hinge] and routed to
//     [box.hinge.general]: the control. Its two nodes are what a routed
//     folded-in class's must match, kind for kind and in the same order,
//     which is the whole of what "routes the way a class outside a region
//     already renders" means.
//
// Self-contained (no #includes) and built from char/short/int only, so it
// parses standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_GATHERED_CLASS_AT_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_GATHERED_CLASS_AT_HPP

// \rSec2[box.syn]{Header <box> synopsis}

namespace demo {

//! \at box.crate
//! \remarks A `crate` owns no storage.
struct crate {
    static_assert(sizeof(int) >= 2);

    int size;
};

//! \at box.pallet
template <class T>
struct pallet {
    static_assert(sizeof(char) == 1);

    T slot;
};

//! \at box.label
//! \remarks A `label` names a `crate` and nothing else.
struct label {
    int id;
};

//! \remarks A `crumb` is described where it is written.
struct crumb {
    static_assert(sizeof(short) >= 2);

    int weight;
};

//! \at box.nosuch
//! \remarks A `strap` fastens two crates together.
struct strap {
    int length;
};

} // namespace demo

/// END [box.syn]

// \rSec3[box.crate]{Class `crate`}

// \rSec3[box.pallet]{Class template `pallet`}

// \rSec3[box.label]{Class `label`}

// \rSec3[box.hinge]{Class `hinge`}

namespace demo {

//! \at box.hinge.general
//! \remarks A `hinge` joins two crates.
struct hinge {
    static_assert(sizeof(int) >= 2);

    int pin;
};

} // namespace demo

// \rSec4[box.hinge.general]{General}

#endif // BEMAN_SPECGEN_CORPUS_SPEC_GATHERED_CLASS_AT_HPP
