// include/beman/specgen/frontend/merged_twins.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Merged twins (design §4.3).
// A conditionally trivial special member is two declarations in C++: a
// defaulted one, constrained to where `T`'s member is trivial, and a
// user-provided one constrained to everywhere else. `\merge` drops the first,
// and the second -- the *survivor* -- stands for both. The pair is one
// specification entity, so the survivor's constraint is what the two share:
// the conjunct that only tells them apart is how the implementation chooses
// between them, not part of the contract. The draft's
// `constexpr optional(const optional&);` has no
// `!is_trivially_copy_constructible_v<T>` in it.
//
// A twin is a sibling with the same name, parameter types and object
// qualifiers whose markup says `\merge`, on the in-class declaration or on an
// out-of-line definition. A survivor conjunct is dropped when it is the
// logical negation of a conjunct of one of its twins, either way round,
// compared as expressions rather than as text.

#ifndef BEMAN_SPECGEN_FRONTEND_MERGED_TWINS_HPP
#define BEMAN_SPECGEN_FRONTEND_MERGED_TWINS_HPP

#include <beman/specgen/synopsis_edits.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <vector>

namespace beman::specgen::frontend {

// Which of `fn`'s trailing-clause conjuncts its merged twins cut, one flag per
// conjunct in source order -- or empty, meaning cut nothing. Everything that
// renders the clause (the synopsis, an itemdecl that keeps it, the derived
// Constraints) asks this one question, so they cannot disagree. The cut is
// made only on a clause that is a flat `&&` chain in the file's own text:
// removing a conjunct from inside parentheses, or from a macro expansion,
// would need more than deleting it and its `&&`.
std::vector<bool>
twin_cut_mask(const clang::FunctionDecl* fn, const clang::SourceManager& sm, const clang::LangOptions& lang_opts);

// The source edits that make `fn`'s trailing requires-clause say what its
// twin cut mask keeps: each cut conjunct goes with the `&&` joining it to its
// neighbour, and when every conjunct is cut, the `requires` keyword goes too.
// The edits are dominant over anything nested in what they remove.
std::vector<synopsis_edits::SynopsisEdit>
twin_cut_edits(const clang::FunctionDecl* fn, const clang::SourceManager& sm, const clang::LangOptions& lang_opts);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_MERGED_TWINS_HPP
