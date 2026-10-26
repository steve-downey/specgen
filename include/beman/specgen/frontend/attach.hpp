// include/beman/specgen/frontend/attach.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Attaching a docblock to the declaration it documents (design §3.3, §4.2):
// the lowered itemdescr, its derived elements, and the extracted
// `\effects-equiv` / `\returns-expr` bodies, as one AttachedItem.
// `\effects-equiv` lowers to a description element carrying an *empty*
// EquivalentTo (lowering::append_extraction); the front end fills its code from
// the out-of-line definition body here. Per design §5.2 ("Consumed asserts are
// excluded from `\effects-equiv` extraction"), the prologue assertion run that
// derive_mandates consumes is deleted here too, while any preceding aliases are
// retained. A Mandates element and an Effects "Equivalent to:" block derived
// from one body therefore do not double-count the asserts.

#ifndef BEMAN_SPECGEN_FRONTEND_ATTACH_HPP
#define BEMAN_SPECGEN_FRONTEND_ATTACH_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/file_views.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
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

// Result of build_spec_item: the SpecItem it built (possibly empty), the
// directives its docblock carried (default-constructed if it had none), and
// whether it attached an out-of-line function definition at all — the caller
// (classify(), the pipeline's first stage) needs that last bit to decide
// whether `\also`/empty-descr grouping and `\omit` apply; a non-function
// decl's empty SpecItem must never be treated as a group primary or a
// candidate for omission.
struct AttachedItem {
    beman::specgen::ir::SpecItem             item;
    beman::specgen::lowering::ItemDirectives directives;
    bool                                     is_function_def = false;
    // Whatever this item's docblock grammar reported, positioned in
    // the main file. Carried out with the item rather than printed here: the
    // front end builds IR and says nothing to a terminal, so reporting is the
    // driver's, reached through document_build::BuildResult::diagnostics.
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
    // Placement key: the byte offset of the item's *in-class*
    // declaration within the main file — getFirstDecl() for an out-of-line
    // definition, the friend/member itself for an in-class one. Both are class-
    // body offsets, so ordering items by this key is class-body order (design
    // §3.3), the axis that interleaves in-class members with their out-of-line
    // siblings.
    unsigned inclass_offset = 0;
    // The file `inclass_offset` is an offset in.  A document can span several
    // files (issue #77), and placement keys are compared across all of them,
    // so the pair is what identifies a position -- item_decl_event turns it
    // into the document offset that ordering actually uses.
    clang::FileID file;
    unsigned      grouping_line = 0; // first line of the docblock carrying grouping metadata
    // \verbatim-itemdecl payload (design §4.3, issue #4): exact, span-free
    // text that *replaces* the extracted declaration — the attach path that
    // would extract one uses this instead when it is engaged, and classify()
    // suppresses the comment's own standalone node so the item appears once.
    std::optional<std::string> verbatim_itemdecl = {};
};

// A documented out-of-line member definition in a source file (issue #149),
// built into its item in the source file's own parse: markup, the body's
// derivations and the itemdecl, exactly as a definition in the header is. The
// header's parse places it, by the in-class declaration it defines, with
// whatever `\ref` group or `\at` routes that declaration. Only the parts of
// `attached` that hold no Clang state cross between the two parses; the header
// side replaces `file` and `inclass_offset` with its own.
struct SourceMarkup {
    AttachedItem attached;
    // The definition's position, for messages naming it.
    std::string file;
    unsigned    line = 0;

    std::string where() const { return std::format("{}:{}", file, line); }
};

// The source files' items, by the canonical in-class declaration each defines
// in the header's parse.
using SourceMarkupMap = std::map<const clang::Decl*, const SourceMarkup*>;

// What bare `\seebelow` on a namespace-scope variable (template) asks for: the
// declared type masked as the draft's *unspecified* placeholder and the
// initializer dropped — the customization-point-object shape (issue #24). The
// targeted forms have no variable meaning, so one is an Error rather than the
// silent no-op the bare form used to be.
//
// Both variable paths read the marker through here. On an ordinary wording item
// it is the itemdecl that carries the mask; marked `\expos` as well, the two
// compose (issue #38) and the standalone synopsis carries it, an
// exposition-only object of unspecified type being how the draft spells a
// helper of this kind. Read on only one of the two paths, the other one
// silently ignored the marker.
struct VariableMask {
    const clang::VarDecl*                                     unspecified = nullptr;
    std::optional<beman::specgen::document_build::Diagnostic> diagnostic  = {};
};

VariableMask variable_seebelow_mask(const clang::Decl*                              decl,
                                    const beman::specgen::lowering::ItemDirectives& directives,
                                    unsigned                                        grouping_line);

// Parse and lower `decl`'s own `//!` docblock into `attached` — descr,
// directives, diagnostics, and the grouping line — if it carries one. The
// shared front half of the attach_* functions whose markup and itemdecl come
// off one decl (attach_alias, attach_record_declaration,
// attach_namespace_entity). attach_function keeps its own copy inline: its
// markup decl and itemdecl decl differ, and it reads the directives
// mid-flight to decide requires-clause stripping.
void attach_docblock(AttachedItem& attached, const clang::Decl* decl, const clang::SourceManager& sm);

// Build the AttachedItem for one function, given the decl its markup/body/
// derivations come from (`def`) and the decl its itemdecl *text* comes from
// (`decl_form`). For an out-of-line definition those differ — markup at the
// definition (clause order), itemdecl from the in-class declaration (design
// §3.3) — so `def` is the definition and `decl_form` is `def->getFirstDecl()`.
// For an in-class definition (a hidden friend or a compiler-bug member)
// the definition *is* its own first declaration, so both are the same decl and
// `friend_begin` is the enclosing FriendDecl's begin location (invalid for a
// plain member) so the itemdecl keeps `friend`.
AttachedItem attach_function(const clang::FunctionDecl*                       def,
                             const clang::FunctionDecl*                       decl_form,
                             clang::SourceLocation                            friend_begin,
                             const clang::SourceManager&                      sm,
                             const clang::LangOptions&                        lang_opts,
                             const std::set<std::string>&                     ns_drop_set,
                             const std::map<const clang::Decl*, std::string>& expos_set,
                             const SkippedRanges&                             skipped);

// `head` is the enclosing TypeAliasTemplateDecl for a namespace-scope alias
// template — the decl the docblock hangs off and the extraction range starts
// at; null for a plain alias, in class or out.
AttachedItem attach_alias(const clang::TypeAliasDecl*                      alias,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::set<std::string>&                     ns_drop_set,
                          const std::map<const clang::Decl*, std::string>& expos_set,
                          const clang::TypeAliasTemplateDecl*              head = nullptr);

// Build the AttachedItem for a documented record declaration the header never
// defines (design §6): an undefined class-template primary — the normal way
// to write an algebra whose operations a model must register, deliberately
// left undefined so an unregistered type fails a concept — or its non-template
// counterpart. Its wording is an ordinary itemdecl: the declaration through
// its semicolon, plus whatever description the docblock carries. `decl` is
// the top-level decl (the ClassTemplateDecl for a template, so the extraction
// starts at `template` and the docblock is looked up where classify's
// definition arms look it up); `record` supplies the class/struct/union tag
// that keeps the template head on its own line. No index metadata: a
// *defined* record's Synopsis carries none either, and the two should index
// alike or not at all.
AttachedItem attach_record_declaration(const clang::NamedDecl*                          decl,
                                       const clang::CXXRecordDecl*                      record,
                                       const clang::SourceManager&                      sm,
                                       const clang::LangOptions&                        lang_opts,
                                       const std::set<std::string>&                     ns_drop_set,
                                       const std::map<const clang::Decl*, std::string>& expos_set);

// Build the AttachedItem for a documented namespace-scope concept, variable,
// or variable template (design §6): like the record declaration above, the
// declaration *is* the wording — extracted whole through its semicolon,
// constraint and initializer kept, the way the draft writes them
// ([concept.same], [tuple.helper]). Aliases go through attach_alias instead,
// which owns the alias masking rules; either way the item indexes as a
// library global, the way a documented free function does.
AttachedItem attach_namespace_entity(const clang::NamedDecl*                          decl,
                                     const clang::SourceManager&                      sm,
                                     const clang::LangOptions&                        lang_opts,
                                     const std::set<std::string>&                     ns_drop_set,
                                     const std::map<const clang::Decl*, std::string>& expos_set);

// Build the AttachedItem for a documented static data member (issue #148): an
// itemdecl of its own and the elements its docblock carries, routed by its
// class's `\ref` group or an `\at`, the way an in-class alias is. The draft
// gives `atomic<T>::is_always_lock_free` and the `numeric_limits` members
// exactly this. `decl` is the VarDecl, or the VarTemplateDecl of a static data
// member template, whose extraction then starts at `template`. The initializer
// stays in the itemdecl unless bare `\seebelow` drops it; unlike a namespace
// variable's, the mask leaves the declared type alone, since a static data
// member's type is part of its interface. The class synopsis keeps the
// initializer either way, as [numeric.limits.general]'s does.
AttachedItem attach_static_member(const clang::NamedDecl*                          decl,
                                  const clang::CXXRecordDecl*                      record,
                                  const clang::SourceManager&                      sm,
                                  const clang::LangOptions&                        lang_opts,
                                  const std::set<std::string>&                     ns_drop_set,
                                  const std::map<const clang::Decl*, std::string>& expos_set);

// Build the SpecItem for a top-level decl that is not a class/class template
// (design §3.3, §6 "Public member, out-of-line def"). `decl` may be a plain
// FunctionDecl or a FunctionTemplateDecl (a member function template of a
// class, or of a class template, defined out-of-line arrives this way; a
// non-template member of a class template, like `box<T>::get()`, does not —
// it is a plain FunctionDecl whose lexical text happens to carry the class's
// template head).
//
// An out-of-line member definition is attached as before. A namespace-scope
// free-function definition is attached only when it carries a docblock: public
// wording is explicit, while unannotated implementation helpers remain absent.
// Both the semantic and lexical contexts must be file contexts. The lexical
// half excludes hidden friends, which are semantic namespace functions but
// belong to collect_inclass_items because their source declaration is in a
// class body. Ordinary in-class definitions are handled there too.
AttachedItem build_spec_item(clang::Decl*                                     decl,
                             const clang::SourceManager&                      sm,
                             const clang::LangOptions&                        lang_opts,
                             const std::set<std::string>&                     ns_drop_set,
                             const std::map<const clang::Decl*, std::string>& expos_set,
                             const SkippedRanges&                             skipped);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_ATTACH_HPP
