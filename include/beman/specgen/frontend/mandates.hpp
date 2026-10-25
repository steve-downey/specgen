// include/beman/specgen/frontend/mandates.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The Mandates derivation (design §5.2) from a function body's leading
// static_asserts.
// [structure.specifications] read backwards: Mandates = ill-formed (not merely
// removed from overload resolution) = the leading static_assert prefix of the
// out-of-line definition body. Consume the *maximal* run of static_asserts at
// the front of the body, flatten each condition at top-level `&&` (reusing
// split_conjuncts), and phrase every conjunct with the same rewriter
// Constraints uses — design §5.3 shares both the phrasing and the
// sentence/itemize rendering across the two derivations. Leading local aliases
// may precede the assertion run; the first statement of any other kind ends the
// prologue, so a static_assert appearing later is not a Mandate. static_assert
// *messages* are dropped, as the design specifies.
//
// This only tags the element `derived` and keeps its pre-join
// conjuncts (ir.hpp's DescriptionElement::conjuncts) -- ordering an authored
// twin after it (canonicalize, ir.cpp), merging the two into one rendered
// block (backend/latex.cpp), and the drift check that compares them
// (validate.cpp) are Tier-A code, not repeated here. Excluding the consumed
// asserts from `\effects-equiv` extraction is shared below with the body
// extraction.

#ifndef BEMAN_SPECGEN_FRONTEND_MANDATES_HPP
#define BEMAN_SPECGEN_FRONTEND_MANDATES_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace beman::specgen::frontend {

struct MandatesPrologue {
    std::vector<const clang::DeclStmt*> assertions;
    const clang::Stmt*                  extraction_first = nullptr;
};

// One shared reading of the body prologue for both Mandates derivation and
// Equivalent-to extraction. Leading local aliases are retained source, but
// permit the immediately following maximal static_assert run to remain
// derivable. The first declaration/statement of any other kind closes the
// prologue; an assertion after that boundary is ordinary body code.
MandatesPrologue mandates_prologue(const clang::CompoundStmt* body);

std::optional<beman::specgen::ir::DescriptionElement>
derive_mandates(const clang::FunctionDecl*                       fn,
                const clang::SourceManager&                      sm,
                const clang::LangOptions&                        lang_opts,
                const std::set<std::string>&                     ns_drop_set,
                const std::map<const clang::Decl*, std::string>& expos_set);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_MANDATES_HPP
