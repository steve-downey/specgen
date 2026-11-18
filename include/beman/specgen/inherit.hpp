// include/beman/specgen/inherit.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — inherited descriptions (docs/plans/draft-import.md, part 3).
//
// A declaration whose docblock says `\asdraft` takes its description from the
// baseline item it pairs with: the working draft's, once `--baseline-draft` has
// imported it. The pass runs after both documents are built and before
// validation and the diff, so everything downstream sees ordinary wording: the
// validator checks the inherited Constraints and Mandates against the
// derivation's conjuncts as it would authored prose, and the differ shows the
// item as unchanged in everything but what the paper changed. Clang-free, like
// `diff`.
#ifndef BEMAN_SPECGEN_INHERIT_HPP
#define BEMAN_SPECGEN_INHERIT_HPP

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/ir.hpp>

#include <string>
#include <vector>

namespace beman::specgen::inherit {

struct Diagnostic {
    Severity    severity = Severity::Error;
    std::string where; // the declaration, as its signature reads
    std::string message;
};

// Whether any item of the document is still waiting to inherit.
bool has_unresolved(const ir::Document&);

// Resolves each `as_draft` item of `current` against `baseline`. Sections pair
// by stable name; an item pairs with the baseline item of its section that
// declares the same names and whose signature reads most like its own, each
// baseline item taken once. When one side gives no names to pair by -- a
// `\verbatim-itemdecl`, or the draft's item for an exposition-only member,
// which its index does not name -- the pair is made by how the declarations
// read, if they read alike. An item with no pair, or a `\covers` for an element
// the resolved description lacks, is an Error and the item is left as it was.
//
// A synopsis marked `\asdraft-synopsis` takes the baseline synopsis of the same
// class and has the specifiers of its own `declared` text written into it
// (synopsis_check.hpp), which is what lets a paper that only adds `constexpr`
// say so without retyping the draft's class. A line for a member the class has
// from a base class takes them from the item that declares it, looked for in
// the sections around the synopsis, nearest first. What cannot be written is
// left for `--validate`, which compares the result with those declarations.
//
// The resolved description is the baseline's, with each element the item
// authors replacing the baseline element of its kind. A derived element (from
// a requires-clause or a static_assert) does not replace the baseline's: its
// conjuncts move onto it, as the validator's evidence the inherited prose is
// checked against; a derived element the baseline has no counterpart for
// stays, being new wording.
//
// Apart from the descriptions, every item that pairs with a baseline item,
// marked or not, has its declaration typeset as the baseline's is: each span
// takes the form (`Span::no_correction`) of the baseline span that says the
// same, where the document left the form to the context. The marked items pair
// first. This is typesetting only, so an item that finds no pair is not a
// finding.
std::vector<Diagnostic> resolve(ir::Document& current, const ir::Document& baseline);

} // namespace beman::specgen::inherit

#endif // BEMAN_SPECGEN_INHERIT_HPP
