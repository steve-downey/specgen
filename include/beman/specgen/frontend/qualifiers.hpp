// include/beman/specgen/frontend/qualifiers.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Reference-resolved namespace mapping (design §3.5).
// "All rewrites act on identifier tokens whose AST referent is known — never
// text match." A qualifier is dropped only when its nested-name-specifier
// *resolves* to a namespace whose fully-qualified name is in the drop set:
// `std` (the draft writes library names unqualified, being inside namespace
// std) and the header's own namespace (`beman::optional`, which maps onto
// `std`). A `detail::` qualifier resolves to something not in the set and is
// left verbatim — the leakage checker is what flags those.
//
// Also the uses of namespace-scope exposition-only declarations, which
// carry their own qualifier ranges for the same reason.

#ifndef BEMAN_SPECGEN_FRONTEND_QUALIFIERS_HPP
#define BEMAN_SPECGEN_FRONTEND_QUALIFIERS_HPP

#include <clang/AST/DeclBase.h>
#include <clang/AST/Decl.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// The fully-qualified name an NNS writes, or nullopt when it is not a pure
// namespace qualifier (a type qualifier, `__super`, a dependent name).
std::optional<std::string> qualifier_namespace_name(clang::NestedNameSpecifier qualifier);

bool imported_qualifier_is_droppable(const clang::NamedDecl*       decl,
                                     clang::NestedNameSpecifierLoc qualifier,
                                     const std::set<std::string>&  drop);

struct ExposUse {
    unsigned    name_begin;
    unsigned    name_end;
    unsigned    qualifier_begin;
    unsigned    qualifier_end;
    std::string display;
    // A parameter use renamed to the first declaration's spelling (issue
    // #160): the name is replaced by `display` as plain text, not an exposid.
    bool plain = false;
};

// Reference-resolved uses of namespace-scope exposition-only declarations
// under a declaration or a statement. `expos` maps a marked declaration's
// canonical decl to its exposition-only display name.
std::vector<ExposUse> expos_uses(clang::Decl*                                     root,
                                 const std::map<const clang::Decl*, std::string>& expos,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts);
std::vector<ExposUse> expos_uses(clang::Stmt*                                     root,
                                 const std::map<const clang::Decl*, std::string>& expos,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts);

// The source ranges of droppable namespace qualifiers under a statement or
// expression subtree (a requires-clause condition, a static_assert condition,
// an extracted body), or under a declaration, less any nested inside an
// exposition-only use's qualifier.
std::vector<std::pair<unsigned, unsigned>>
namespace_qualifier_edits(clang::Stmt*                                     root,
                          const std::set<std::string>&                     drop,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::map<const clang::Decl*, std::string>& expos);

std::vector<std::pair<unsigned, unsigned>>
namespace_qualifier_edits(clang::Decl*                                     root,
                          const std::set<std::string>&                     drop,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::map<const clang::Decl*, std::string>& expos);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_QUALIFIERS_HPP
