// include/beman/specgen/frontend/docblock_lookup.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Finding a declaration's docblock and reading it: which comment vocabulary
// a line opens (design §1), where the markup starts in a merged raw comment,
// the comment attached to a declaration, and what its markers say.

#ifndef BEMAN_SPECGEN_FRONTEND_DOCBLOCK_LOOKUP_HPP
#define BEMAN_SPECGEN_FRONTEND_DOCBLOCK_LOOKUP_HPP

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>

#include <clang/AST/DeclBase.h>
#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/ADT/StringRef.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace beman::specgen::frontend {

// Which of the three comment vocabularies a line opens, read after leading
// horizontal whitespace.
//
// `//!` and `/*!` are **specgen's** markup: the docblock grammar parses them
// and design §3.4/§4 says to strip them from a synopsis. `///` and `/**` are
// **Doxygen's**, and specgen reads neither — a header written for Doxygen has
// prose where specgen's grammar expects an element tag, so parsing one
// produces a `prose before first element tag` error per block and an
// itemdescr made of API documentation. Everything else — a plain `//` or
// `/* */` — is **draft-form** text: a `\ref` group header, a `// see below`,
// a license block. That kind is kept verbatim.
//
// Doxygen's two spellings are nonetheless stripped from a synopsis, which is
// why this is a three-way classification and not a bool. The draft never
// prints implementation documentation, and a `///` line that merely stopped
// being markup would start *surviving* into the wording instead — a quieter
// version of the same defect.
enum class CommentVocabulary { Draft, Markup, Doxygen };

CommentVocabulary line_vocabulary(llvm::StringRef rest);

// Byte offset within `raw` of the first *docblock* line — specgen markup only
// — or nullopt if the comment carries no markup at all. What the grammar is
// pointed at.
std::optional<std::size_t> docblock_start(llvm::StringRef raw);

// Byte offset within `raw` of the first line a synopsis must not show —
// specgen markup *or* Doxygen. What extract_synopsis cuts from.
std::optional<std::size_t> stripped_comment_start(llvm::StringRef raw);

// Does `raw` carry markup anywhere (see docblock_start)?
bool is_docblock_comment(llvm::StringRef raw);

// Translate a decl's docblock findings into document_build::Diagnostics
// (design §9's "diagnostics are golden text"): same severity, same
// message, with the docblock-relative line turned into a main-file line so a
// reader can go to it.
//
// The arithmetic is addition, not a lookup, and that is a property of how the
// text got here: the caller hands parse_docblock a *contiguous slice* of the
// file (the raw comment from `docblock_start` on), and
// strip_comment_decorations rewrites each line in place without adding or
// removing any, so docblock line N is main-file line (line of the slice's
// first character) + N - 1. A
// diagnostic with line 0 is one of the whole-block cross-checks (`\effects`
// and `\effects-equiv` are mutually exclusive, and its `\returns` twin), which
// belong to no single line; they report against the docblock's first line
// rather than against line 0 of the file, which does not exist.
std::vector<beman::specgen::document_build::Diagnostic>
docblock_diagnostics(const clang::RawComment*                rc,
                     std::size_t                             markup_start,
                     const std::vector<grammar::Diagnostic>& diags,
                     const clang::SourceManager&             sm);

// The raw comment attached to `decl`, looked up through its described
// template when it has one. Clang anchors comment search for a
// RedeclarableTemplateDecl at its `template` keyword but for the templated
// decl at its *name*, and rejects any candidate comment separated from the
// anchor by one of `;{}#@` (ASTContext::getRawCommentNoCacheImpl). A
// requires-expression in a template-head constraint —
// `requires requires(T t) { ... }` — puts all three between the header and
// the name, so a lookup through the templated decl silently loses the
// docblock that the template decl still finds (issue #20). The described
// template is also the decl clang's own comment machinery documents
// (adjustDeclToTemplate).
//
// The key is an ASTContext::RawCommentLookupKey, which LLVM 23 widened from
// `const Decl*` so that macros can carry comments too; the conversion here is
// the PointerUnion's, not a coincidence. The lookup stays the *NoCache* one on
// purpose: getRawCommentForAnyRedecl, the public alternative whose signature
// happens to span both LLVM 22 and 23, walks the redeclaration chain and finds
// comments this anchor rule is written to reject.
const clang::RawComment* attached_raw_comment(const clang::Decl* decl);

// Does `decl` carry a `//!`/`/*!` docblock of its own? The two-step the
// attach path and the roster both need before trusting a raw comment:
// attached_raw_comment yields whatever comment immediately precedes
// the decl, which may well be a draft-form `\ref`/`\rSec` line that belongs to
// the synopsis rather than markup for this entity.
bool has_docblock(const clang::Decl* decl, const clang::SourceManager& sm);

// Lower just the directives (markers) from a decl's `//!` docblock, or a
// default-constructed set if it has none. Used by the omit-set and
// expos-set pre-passes to read `\omit`/`\merge`/`\expos` without building a full
// itemdescr; attach_function lowers the descr and directives together on the
// attach path. Takes any Decl so it serves data members (`\expos`) as well as
// functions.
beman::specgen::lowering::ItemDirectives docblock_directives(const clang::Decl* decl, const clang::SourceManager& sm);

bool suppresses_generated_entity(const beman::specgen::lowering::ItemDirectives& directives);

// A top-level record marked \omit/\merge contributes no synopsis or derived
// class wording. Return an engaged diagnostics vector only for that case, so
// classify() can distinguish an unmarked record from a cleanly marked one
// while preserving every finding from the marker's own docblock.
std::optional<std::vector<beman::specgen::document_build::Diagnostic>>
record_suppression_diagnostics(const clang::Decl* decl, const clang::SourceManager& sm);

// Everything a class or class-template *definition*'s own `//!` docblock
// contributes (design §3.4, §6): the description elements the author wrote
// about the type itself, a `\verbatim-synopsis` payload replacing extraction
// (design §4.3, issue #4), and the block's grammar findings.
//
// All three used to be conditional on a verbatim marker, so an ordinary class
// docblock produced nothing and said nothing — issue #18's silent loss. It is
// the one shape issue #1's backstop cannot catch: a definition *does* produce
// wording (its synopsis), so the docblock looks accounted for while its
// contents are gone. The findings ride along here because classify()
// suppresses the comment's own RawItem for an attached block, so this is
// where they reach the driver.
struct RecordDocblock {
    beman::specgen::ir::ItemDescr                           descr;
    std::optional<std::string>                              synopsis;
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
};

RecordDocblock record_docblock(const clang::Decl* decl, const clang::SourceManager& sm);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_DOCBLOCK_LOOKUP_HPP
