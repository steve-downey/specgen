// include/beman/specgen/frontend/directive_sets.hpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The pre-pass sets read from markers before classify() runs: what to omit
// or merge, what is exposition-only, which namespaces map onto `std`, and
// what `\seebelow` and `\freestanding` mark.

#ifndef BEMAN_SPECGEN_FRONTEND_DIRECTIVE_SETS_HPP
#define BEMAN_SPECGEN_FRONTEND_DIRECTIVE_SETS_HPP

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

// The canonical declarations to drop from every synopsis (design §4.3):
// members whose markup carries `\omit` or `\merge`. Built as a pre-pass because
// a class's synopsis is extracted the moment the class is seen — before the
// out-of-line definition carrying the marker is reached in the event stream —
// so extract_synopsis needs the set up front. Scans both out-of-line
// definitions (keyed to their in-class declaration via getFirstDecl()) and
// in-class members; keys on getCanonicalDecl() so the check in extract_synopsis
// matches whichever redeclaration it walks.
std::set<const clang::Decl*> build_omit_set(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm);

// Namespace-owned entities that can be rendered as free-standing exposition
// synopses. isFileContext includes the global namespace and named namespaces,
// while excluding an out-of-line definition of a static data member.
// Class templates belong here too (issue #23): the draft's exposition-only
// helpers are spelled as class templates as well as alias templates, and
// their template-id uses resolve through the same TemplateName hook the
// alias-template uses take. Plain classes too (issue #142), whose uses are
// TagTypeLocs.
bool is_namespace_expos_candidate(const clang::Decl* decl);

// A class definition among the candidates above: a class template whose
// templated record is defined, or a class-template specialization that is.
// Being exposition-only changes how such a class is *named* and nothing else,
// so it is extracted like any other class (design §3.4) rather than copied
// through as a free-standing declaration.
bool is_defined_record(const clang::Decl* decl);

// The documented-wording counterpart: the same namespace-owned entity kinds,
// asked about by a docblock with description content rather than `\expos` —
// ordinary wording items (design §6) instead of standalone synopses. The
// isFileContext guard excludes the out-of-line definition of a class's own
// member (a static data member, a member variable template), whose wording
// belongs to its class.
//
// An enumeration is one of them (issue #68). It is a type whose definition is
// its interface, so the declaration is the itemdecl and the docblock's
// description follows — the draft's shape for [fs.enum.file.type], whose
// wording is a two-column table of enumerator meanings and which `\lib2dtab2`
// already renders. It was the last kind a library could document and get
// nothing for: an error said so rather than dropping it silently, but an
// error is not a way to specify an enum, and there was no other.
const clang::NamedDecl* as_namespace_entity(const clang::Decl* decl);

// The exposition-only members (design §4.3/§3.5): canonical member decl →
// its `\exposid` display name (`\expos(name)` override, else `exposid_name` of the
// identifier). A pre-pass like build_omit_set, so extract_synopsis has the set
// when a class is first seen. Covers data members, member functions, and the
// namespace-scope concepts/variables/variable templates/aliases/alias
// templates whose resolved uses are rewritten as exposids
// (extract_namespace_expos_synopsis emits their free-standing declarations).
std::map<const clang::Decl*, std::string> build_expos_set(const std::vector<clang::Decl*>& decls,
                                                          const clang::SourceManager&      sm);

// The namespaces whose qualifiers are dropped from rendered code (design
// §3.5): `std` — the draft writes library names unqualified, being inside
// namespace std — plus every namespace the header itself declares its top-level
// entities in (`demo`, `beman::optional`), which maps onto `std` in the draft
// and so is likewise implicit. Everything else (notably `detail::`) is left
// verbatim for the leakage checker.
//
// "Top-level" is prefix-minimal, and has to be. collect_top_level_decl
// descends through nested namespaces, so the raw paths include `demo::detail`
// — the implementation namespace, whose qualifier this set exists to *keep*.
// Keeping that path would let the two spellings of one qualifier disagree:
// qualifier_namespace_name yields the chain as *written* (`detail`), which
// never equals the fully-qualified path (`demo::detail`), so `detail::storage`
// would stay while `demo::detail::storage` lost its qualifier entirely,
// hiding the leak instead of leaving it visible. Dropping every path that has
// another in the set as a proper prefix makes both spellings render the same
// way, because the prefix walk does the rest: an unmatched `demo::detail`
// chain is descended into, its `demo` prefix matches, and what is left is
// `detail::storage` either way. The same narrowing is what lets a real
// `std::ranges::` qualifier render as the draft writes it, `ranges::`.
std::set<std::string> build_namespace_drop_set(const std::vector<clang::Decl*>& decls);

// The function operands the synopsis/itemdecl renders as `\seebelow`
// (design §4.3): canonical decls map to the selected operand.
// Same pre-pass
// shape as build_omit_set (out-of-line defs keyed to getFirstDecl(), in-class
// members to themselves), so extract_synopsis has the set when a class is seen.
SeeBelowMap build_seebelow_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm);

// The literal draft synopsis suffix requested by \freestanding or
// \freestanding-deleted. Like SeeBelowMap, this is transient front-end
// metadata keyed to the canonical in-class declaration: an out-of-line
// definition commonly carries the markup, but the earlier class synopsis is
// where the comment must be emitted.
FreestandingMap build_freestanding_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm);

// The namespaces below the document's own that are part of the specified
// interface (design §3.5, issue #209): every namespace some redeclaration of
// which carries `\specified` as its docblock, by qualified path
// (`demo::views`). A namespace is neither dropped nor foreign by itself, and
// nothing in the AST says whether `views` is interface or `detail` is
// implementation, so the author says. Read across the whole translation
// unit rather than the document's decls: a project marks a namespace once,
// in a header every other header includes, and every document in the paper
// sees it -- the same reach `\expos` has into an included header (issue #36).
//
// Two findings ride out with the set. `\specified` on anything but a
// namespace is an Error, as `\verbatim-synopsis` off a class is; and a
// namespace's docblock carrying anything else -- another marker, or
// description text -- is an Error too, a namespace having no wording of its
// own for the text to be.
struct SpecifiedNamespaces {
    // Namespace docblocks consumed here must not be diagnosed as detached.
    std::vector<const clang::NamespaceDecl*>                marked;
    std::set<std::string>                                   paths;
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
};
SpecifiedNamespaces build_specified_namespace_set(const std::vector<clang::Decl*>& decls,
                                                  clang::ASTContext&               ctx,
                                                  const clang::SourceManager&      sm);

// Where a declaration renders, below `std` (design §3.5): the longest prefix
// of its namespace path, past the drop-set prefix the mapping makes `std`,
// every component of which is specified. A namespace rooted in `std` itself
// is specified without a marker -- a `std::ranges` specialization renders
// inside `namespace ranges {`, which is the standard's own vocabulary, the
// same guard the leakage checker's qualifier half already applies. An
// unmarked `detail` under `whatwg` flattens into `whatwg`, exactly as an
// unmarked `detail` under the document's namespace flattens into `std`.
//
// Two spellings of one path: `qualifier` is what an itemdecl's declarator
// takes (`views`, `literals::chrono_literals`) and `definition` is what a
// nested-namespace-definition writes (`inline literals::inline
// chrono_literals`), the draft's own form for both. Both empty at the top.
struct RenderedNamespace {
    std::string qualifier;
    std::string definition;
};
RenderedNamespace rendered_namespace(const clang::Decl*           decl,
                                     const std::set<std::string>& ns_drop_set,
                                     const std::set<std::string>& specified);

// The qualifier an itemdecl's declarator-id takes, or empty: the rendered
// namespace of a namespace-scope function or variable (template), the way
// the draft writes `ranges::copy` and `filesystem::absolute`. A type -- a
// concept, an alias, an enumeration, a class -- stays bare, as the grammar
// requires and the draft writes them. A declarator that already writes a
// qualifier takes no second one: the mapping renders what was written.
std::string declarator_qualifier(const clang::NamedDecl*      decl,
                                 const std::set<std::string>& ns_drop_set,
                                 const std::set<std::string>& specified);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_DIRECTIVE_SETS_HPP
