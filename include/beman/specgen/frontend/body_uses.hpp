// include/beman/specgen/frontend/body_uses.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// What a body the wording does not show still names (design §9).

#ifndef BEMAN_SPECGEN_FRONTEND_BODY_USES_HPP
#define BEMAN_SPECGEN_FRONTEND_BODY_USES_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/document_files.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// Does `fn` carry wording whose body the tool leaves unrendered? Three
// conditions, each excluding a body whose contents are somebody else's
// question:
//
//   - it has a docblock: an undocumented function's body specifies nothing,
//     so what it reaches for is an implementation detail all the way down;
//   - the docblock does not say `\omit`/`\merge`: the same, one step later —
//     the entity is declared but deliberately unspecified;
//   - the docblock carries no extraction marker: an extracted body *is*
//     wording, so a hidden name in it is design §9's **error** case, which
//     the leakage checker reports off the rendered `EquivalentTo` text.
bool has_unextracted_body(const clang::FunctionDecl* fn, const clang::SourceManager& sm);

// Every (documented function with an unrendered body, member it names) pair in
// the header. Both halves of the same two-part walk every marker pre-pass
// makes — out-of-line definitions from `decls`, in-class definitions and
// hidden friends from each record's members — since a body is a body wherever
// it is written.
// The members one function's body names, paired with the label that names the
// body. Pairs rather than BodyUses so a set can order them without a
// comparator; the two are the same two strings.
std::vector<std::pair<std::string, std::string>>
body_member_uses(const clang::FunctionDecl* fn, std::function<bool(const clang::CXXRecordDecl*)> in_document);

std::vector<beman::specgen::ir::BodyUse> collect_unextracted_uses(const std::vector<clang::Decl*>& decls,
                                                                  const clang::SourceManager&      sm);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_BODY_USES_HPP
