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
    std::string where;   // the declaration, as its signature reads
    std::string message;
};

// Whether any item of the document is still waiting to inherit.
bool has_unresolved(const ir::Document&);

// Resolves each `as_draft` item of `current` against `baseline`. Sections pair
// by stable name; an item pairs with the baseline item of its section that
// declares the same names and whose signature reads most like its own, each
// baseline item taken once. An item with no pair, or a `\covers` for an element
// the resolved description lacks, is an Error and the item is left as it was.
//
// The resolved description is the baseline's, with each element the item
// authors replacing the baseline element of its kind. A derived element (from
// a requires-clause or a static_assert) does not replace the baseline's: its
// conjuncts move onto it, as the validator's evidence the inherited prose is
// checked against; a derived element the baseline has no counterpart for
// stays, being new wording.
std::vector<Diagnostic> resolve(ir::Document& current, const ir::Document& baseline);

} // namespace beman::specgen::inherit

#endif // BEMAN_SPECGEN_INHERIT_HPP
