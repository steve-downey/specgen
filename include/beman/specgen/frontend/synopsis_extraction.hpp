// include/beman/specgen/frontend/synopsis_extraction.hpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Subtractive synopsis extraction (design §3.4): a class's, or a
// namespace-scope declaration's, own text, with markup stripped, bodies
// spliced away, and the masks and rewrites applied as edits, then formatted
// in the draft's style.

#ifndef BEMAN_SPECGEN_FRONTEND_SYNOPSIS_EXTRACTION_HPP
#define BEMAN_SPECGEN_FRONTEND_SYNOPSIS_EXTRACTION_HPP

#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

// Subtractive synopsis extraction (design §3.4): lex the class's own text out
// of the main file, then remove/splice exactly two things — nothing else —
// and (design §3.6 step 2) reformat the result with the draft
// FormatStyle. Token rewriting (§3.5, §3.6 step 1) is applied through the
// sentinel edits accumulated below.
//
// `record` supplies the member list to walk (bodies to splice, docblocks to
// strip) and, absent `head_decl`, the extraction range's begin location too.
// For an ordinary class that is the same decl either way. For a class
// template, `record` is the *templated* CXXRecordDecl (members still hang off
// it) but the synopsis has to start at the `template` keyword, which belongs
// to the enclosing ClassTemplateDecl — pass that as `head_decl` so the
// extraction range covers `template <...> ... class ... { ... };` whole.
//
// `omit_set` holds the canonical declarations of members to remove from
// the synopsis entirely — `\omit`ted decls and `\merge`d twins (design §4.3) —
// keyed on getCanonicalDecl() so an out-of-line marker matches the in-class
// declaration walked here.
//
// `expos_set` maps a member's canonical declaration to its
// exposition-only display name (design §4.3, §3.5): such a member renders with
// its declared name replaced by an `\exposid` span and a trailing
// `// exposition only` comment.
beman::specgen::ir::CodeText extract_synopsis(const clang::CXXRecordDecl*                      record,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              const std::set<const clang::Decl*>&              omit_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set,
                                              const SeeBelowMap&                               seebelow_map,
                                              const FreestandingMap&                           freestanding_map,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const clang::Decl*                               head_decl = nullptr);

// The masks a marked declaration's own spelling takes, applied to one
// extraction's edit list. Two of them, and they are the same idea told about
// different halves of a declaration: `unspecified` is the variable whose
// declared *type* is the implementation (issue #24), `see_below` the range --
// an alias's right-hand side, a concept's constraint-expression -- of an
// entity whose *definition* is (issue #50). Both are dominant edits, so a
// qualifier drop or expos-use rewrite inside the masked spelling loses to the
// mask rather than tripping the watermark.
//
// Shared, because a declaration is extracted by more than one path and the
// masks must not be one of the things that differ: a variable folded into a
// gathered header synopsis goes through extract_header_declaration rather than
// extract_freestanding_declaration, and used to come out unmasked, initializer
// and `detail::` and all, with the leak then reported against the marked
// declaration (issue #55).
void add_declaration_masks(std::vector<synopsis_edits::SynopsisEdit>&       edits,
                           std::map<std::string, span_sentinels::SpanInfo>& sentinels,
                           unsigned&                                        span_n,
                           const clang::VarDecl*                            unspecified,
                           std::optional<clang::SourceRange>                see_below,
                           llvm::StringRef                                  buffer,
                           unsigned                                         decl_begin,
                           unsigned                                         decl_end,
                           const clang::SourceManager&                      sm,
                           const clang::LangOptions&                        lang_opts);

// One free-standing namespace-scope declaration, extracted whole: the
// template head and initializer/constraint through the trailing semicolon,
// with the same qualifier drops and expos-use sentinels as class synopses.
// A documented namespace entity's itemdecl and a documented static data
// member's (attach_static_member) take it as written. The `\expos` standalone synopsis
// (extract_namespace_expos_synopsis below) passes `exposition` to rewrite the
// declared name as an `\exposid` span and append the draft's exposition-only
// comment. A documented record declaration the header never defines (an
// undefined class-template primary — classify_record_declaration below)
// passes `record_tag` instead, keeping the draft's template-head line break
// the same way extract_synopsis does, and takes the text verbatim: the
// declaration *is* the wording, an itemdecl rather than a Synopsis.
// `unspecified` (issue #24) is the variable whose declared type bare
// `\seebelow` masks as the draft's *unspecified* placeholder, with the
// initializer dropped — the customization-point-object shape,
// `inline constexpr unspecified name;`. Null for every other caller.
// `uninitialized` is the static data member whose bare `\seebelow` drops its
// initializer and nothing else (issue #148), as [numeric.limits.members]
// writes `static constexpr bool is_specialized;`.
beman::specgen::ir::CodeText
extract_freestanding_declaration(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                 bool                                             exposition,
                                 std::optional<std::string_view>                  record_tag    = std::nullopt,
                                 const clang::VarDecl*                            unspecified   = nullptr,
                                 std::optional<clang::SourceRange>                see_below     = std::nullopt,
                                 const clang::VarDecl*                            uninitialized = nullptr);

// A free-standing exposition-only entity is already a complete
// definition, so it uses the ordinary Synopsis IR rather than inventing an
// itemdecl or a fragment-shaped node. Keep the declaration's template head and
// initializer/constraint through its semicolon, rewrite the declared name and
// any references to other exposition-only entities with the same sentinels as
// class synopses, and append the draft's exposition-only comment.
//
// `unspecified` is the variable whose bare `\seebelow` composes with the
// `\expos` that brought it here (issue #38): the declared type is masked and
// the initializer dropped in the standalone synopsis, exactly as they are in an
// unmarked variable's itemdecl. Null for every other kind, and for a variable
// carrying no such marker.
beman::specgen::ir::CodeText
extract_namespace_expos_synopsis(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                 const clang::VarDecl*                            unspecified = nullptr,
                                 std::optional<clang::SourceRange>                see_below   = std::nullopt);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_SYNOPSIS_EXTRACTION_HPP
