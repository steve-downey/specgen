// src/beman/specgen/frontend/classify.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/classify.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/markers.hpp>
#include <beman/specgen/parse/parser.hpp>
#include <beman/specgen/rsec.hpp>
#include <beman/specgen/section_markers.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/class_head_mandates.hpp>
#include <beman/specgen/frontend/coverage_roster.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/directive_sets.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/inclass_items.hpp>
#include <beman/specgen/frontend/synopsis_extraction.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

using grammar::parse_rsec;
using grammar::rsec_tag_recognized;
using grammar::section_end_fence;
using grammar::SectionHeader;

namespace {

// classify() converts one RawItem into a document_build::DocEvent (decision
// document-build-stages' closed, clang-free variant): the dyn_cast if/else
// chain over clang::Decl
// subclasses is inherently Tier B and is quarantined here — this is the only
// function downstream of RawItem collection that still touches a
// clang::Decl*. build_document() calls it once per RawItem and hands the
// resulting events to document_build::build_tree, clang-free from here on
// (see include/beman/specgen/document_build.hpp, including its top-of-file
// note on why \also/empty-descr grouping is decided partly here and partly
// inside build_tree rather than as a later pass over the finished tree).
namespace db = beman::specgen::document_build;

struct UnrecognizedSectionHeader {
    std::string stable_name;
    unsigned    line_offset = 0;
};

std::optional<UnrecognizedSectionHeader> unrecognized_section_header(std::string_view raw);

// Fold one attached item into its DocEvent: classify()'s shared tail for
// every top-level decl that becomes an ItemDecl — an out-of-line or free
// function definition, or a documented record declaration the header never
// defines. Kept out of classify() so both arms make the same
// `\omit`/`\merge` and grouping decisions rather than drifting apart.
beman::specgen::document_build::DocEvent item_decl_event(AttachedItem&& attached, const DocumentFiles& doc) {
    namespace db = beman::specgen::document_build;

    if (suppresses_generated_entity(attached.directives))
        // \omit / \merge (design §4.3): no itemdescr. Removal from the
        // synopsis is handled by the omit-set pre-pass (build_omit_set +
        // extract_synopsis), which ran before this decl was reached. The
        // docblock's own findings still travel: the entity is
        // unspecified deliberately, but a malformed marker in the block that
        // says so is not deliberate.
        return db::Ignored{std::move(attached.diagnostics)};

    // This is only the "does this item *want* to join" half of \also/
    // empty-descr grouping — a property of this item alone. Whether a join
    // actually happens depends on tree-adjacency context this function does
    // not have (is there a preceding primary in the same open \rSec frame,
    // in push order); that half belongs to document_build::build_tree, which
    // evaluates it before this item's descr is touched by anything, so a
    // follower that turns out to have no primary keeps its own content
    // (design §4.3; see document_build.hpp's top-of-file note for the two
    // review findings this split fixes: a follower's content must not be
    // discarded just because it *asked* to join, and the join must be
    // decided in push order, not the placement-key-sorted order the tree
    // ends up in).
    const bool named_grouping = attached.directives.group_id || attached.directives.also_target;
    const bool wants_join     = !named_grouping && (attached.directives.also || attached.item.descr.elements.empty());

    return db::ItemDecl{doc.offset_in(attached.file, attached.inclass_offset),
                        wants_join,
                        std::move(attached.item),
                        std::move(attached.diagnostics),
                        std::move(attached.directives.group_id),
                        std::move(attached.directives.also_target),
                        attached.grouping_line};
}

// A record declaration that is not a definition. Three cases, none of which
// is the empty Synopsis node this used to produce (whose rendering was an
// empty code block — worse than nothing in a paper):
//
//  - The entity is defined elsewhere: the definition's own event carries the
//    synopsis, and this redeclaration is plumbing. No node, silent.
//  - Documented and never defined: an undefined class-template primary (or
//    plain record) whose declaration *is* the wording — an ordinary itemdecl
//    plus the docblock's description, through item_decl_event so
//    `\omit`/`\merge` and `\also` grouping mean what they mean everywhere
//    else.
//  - Undocumented and never defined: omitted, silent — the same treatment
//    every unmarked entity gets (design §6).
beman::specgen::document_build::DocEvent
classify_record_declaration(const clang::NamedDecl*                          decl,
                            const clang::CXXRecordDecl*                      record,
                            const clang::SourceManager&                      sm,
                            const DocumentFiles&                             doc,
                            const clang::LangOptions&                        lang_opts,
                            const std::set<std::string>&                     ns_drop_set,
                            const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace db = beman::specgen::document_build;

    if (record == nullptr || record->hasDefinition())
        return db::Ignored{};
    if (!has_docblock(decl, sm))
        return db::Ignored{};
    return item_decl_event(attach_record_declaration(decl, record, sm, lang_opts, ns_drop_set, expos_set), doc);
}

bool is_attached_comment(unsigned offset, const AttachedCommentRanges& ranges) {
    return std::ranges::any_of(
        ranges, [offset](const auto& range) { return offset >= range.first && offset <= range.second; });
}

// Recognize the other section-heading convention real headers use,
// `// 22.5.3.3 Destructor[optional.dtor]`. It cannot open a section because it
// carries no depth, but silently treating it as ordinary prose files every
// following declaration into the previous section.
//
// This is intentionally a warning heuristic, not a second section grammar.
// Requiring numbered heading text and a final multi-component dotted name
// avoids ordinary prose references (`[structure.specifications]`), synopsis
// group headers (`\ref{...}`), trailing references, and Doxygen's
// `/// END [optional.syn]`. RawComment merges consecutive `//` lines, so scan
// each physical line and retain its offset for the diagnostic location.
std::optional<UnrecognizedSectionHeader> unrecognized_section_header(std::string_view raw) {
    const auto is_name_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '+';
    };
    const auto is_dotted_name = [&](std::string_view name) {
        return name.contains('.') && name.front() != '.' && name.back() != '.' && !name.contains("..") &&
               std::ranges::all_of(name, [&](char c) { return c == '.' || is_name_char(c); });
    };

    std::size_t line_begin  = 0;
    unsigned    line_offset = 0;
    // substrate generic algorithm: scanning preserves both the byte position
    // in the merged RawComment and the physical-line offset used by the
    // diagnostic; views::split would discard the first and require a second
    // stateful pass to recover the latter.
    while (line_begin <= raw.size()) {
        const std::size_t newline  = raw.find('\n', line_begin);
        const std::size_t line_end = newline == std::string_view::npos ? raw.size() : newline;
        std::string_view  line     = raw.substr(line_begin, line_end - line_begin);

        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos) {
            line.remove_prefix(first);
            if (line_vocabulary(llvm::StringRef{line}) == CommentVocabulary::Draft && line.starts_with("//")) {
                line.remove_prefix(2);
                const std::size_t body_begin = line.find_first_not_of(" \t");
                if (body_begin != std::string_view::npos) {
                    line.remove_prefix(body_begin);
                    const std::size_t body_end = line.find_last_not_of(" \t\r");
                    line                       = line.substr(0, body_end + 1);
                    const std::size_t open     = line.rfind('[');
                    if (open != std::string_view::npos && line.ends_with(']') && line.front() >= '0' &&
                        line.front() <= '9') {
                        const std::string_view stable = line.substr(open + 1, line.size() - open - 2);
                        if (is_dotted_name(stable))
                            return UnrecognizedSectionHeader{std::string(stable), line_offset};
                    }
                }
            }
        }

        if (newline == std::string_view::npos)
            break;
        line_begin = newline + 1;
        ++line_offset;
    }
    return std::nullopt;
}

} // namespace

const clang::FunctionDecl* declared_free_function(const clang::Decl* decl) {
    const clang::FunctionDecl* fn = llvm::dyn_cast_or_null<clang::FunctionDecl>(decl);
    if (fn == nullptr) {
        if (const auto* tmpl = llvm::dyn_cast_or_null<clang::FunctionTemplateDecl>(decl))
            fn = tmpl->getTemplatedDecl();
    }
    if (fn == nullptr || llvm::isa<clang::CXXMethodDecl>(fn) || fn->isThisDeclarationADefinition() ||
        !fn->getDeclContext()->isFileContext() || !fn->getLexicalDeclContext()->isFileContext())
        return nullptr;
    return fn;
}

db::DocEvent classify(const RawItem&                                   ev,
                      const clang::SourceManager&                      sm,
                      const DocumentFiles&                             doc,
                      const clang::LangOptions&                        lang_opts,
                      const std::set<const clang::Decl*>&              omit_set,
                      const std::map<const clang::Decl*, std::string>& expos_set,
                      const SeeBelowMap&                               seebelow_map,
                      const FreestandingMap&                           freestanding_map,
                      const std::set<std::string>&                     ns_drop_set,
                      const SkippedRanges&                             skipped,
                      const AttachedCommentRanges&                     attached_comments,
                      const SourceMarkupMap&                           source_markup) {
    namespace ir = beman::specgen::ir;

    if (ev.decl == nullptr) {
        if (ev.comment_text.find("\\verbatim-synopsis") != std::string::npos ||
            ev.comment_text.find("\\verbatim-itemdecl") != std::string::npos) {
            const std::optional<std::size_t> start = docblock_start(ev.comment_text);
            if (start) {
                const std::string_view markup = std::string_view(ev.comment_text).substr(*start);
                grammar::ParseResult   parsed = grammar::parse_docblock(markup);
                // Attached to a declaration — and not `\omit`/`\merge`d away
                // there — the block is that declaration's own docblock: the
                // declaration's event substitutes the authored text and
                // reports the block's findings, and a second, standalone
                // node here was the duplication (issue #4). An
                // `\omit`/`\merge`d declaration contributes no node of its
                // own, so for that pairing the standalone node still speaks,
                // exactly as it does for a detached block whose entity is
                // never declared (spec_verbatim_record_merge.hpp).
                if ((parsed.block.verbatim_itemdecl || parsed.block.markers.verbatim_synopsis) &&
                    is_attached_comment(ev.offset, attached_comments) &&
                    !suppresses_generated_entity(parsed.block.markers))
                    return db::Ignored{};
                const unsigned first_line = document_line(sm, doc, ev.offset) +
                                            static_cast<unsigned>(std::ranges::count(
                                                std::string_view(ev.comment_text).substr(0, *start), '\n'));
                std::vector<db::Diagnostic> diagnostics =
                    parsed.diags | std::views::transform([first_line](const grammar::Diagnostic& diagnostic) {
                        const unsigned line =
                            diagnostic.line > 0 ? first_line + static_cast<unsigned>(diagnostic.line) - 1 : first_line;
                        return db::Diagnostic{diagnostic.severity, line, diagnostic.message};
                    }) |
                    std::ranges::to<std::vector<db::Diagnostic>>();

                if (parsed.block.verbatim_itemdecl) {
                    lowering::Lowered lowered = lowering::lower(parsed.block);
                    ir::SpecItem      item;
                    item.decl.signatures.push_back(ir::CodeText{std::move(*parsed.block.verbatim_itemdecl), {}});
                    item.descr = std::move(lowered.descr);
                    return db::ItemDecl{ev.offset, false, std::move(item), std::move(diagnostics)};
                }
                if (parsed.block.markers.verbatim_synopsis) {
                    db::SynopsisDecl out;
                    out.offset        = ev.offset;
                    out.synopsis.code = ir::CodeText{parsed.block.verbatim_synopsis.value_or(""), {}};
                    out.exact         = true;
                    out.diagnostics   = std::move(diagnostics);
                    return out;
                }
            }
        }

        if (const auto end = section_end_fence(ev.comment_text);
            end && !std::string_view(end->stable).ends_with(".syn")) {
            const unsigned fence_offset = ev.offset + static_cast<unsigned>(end->line_begin);
            const auto [file, local]    = doc.locate(fence_offset);
            return db::SectionClose{end->stable, sm.getLineNumber(file, local), diagnostic_file_name(sm, file)};
        }

        const parse::parse_result<SectionHeader> header = parse_rsec(ev.comment_text);
        if (!header) {
            if (rsec_tag_recognized(ev.comment_text)) {
                // The \rSec tag itself matched; the failure is in the
                // depth/[stable]/{title} grammar after it -- a malformed
                // \rSec. The comment still contributes no node (design
                // §3.2), but the fact is reported rather than silently
                // dropped. Two positions
                // are worth having: Diagnostic::offset locates the comment in
                // the file (what the driver prints as the location), while
                // the parse failure's own offset locates the bad character
                // inside it.
                const parse::parse_error& failure = header.error();
                // The comment's own line in the main file: RawItem carries a
                // byte offset (its place in the interleave), and the line is
                // what the driver prints.
                return db::Ignored{{db::Diagnostic{beman::specgen::Severity::Warning,
                                                   document_line(sm, doc, ev.offset),
                                                   std::format("malformed \\rSec marker: {} (comment offset {})",
                                                               failure.message,
                                                               failure.where.offset)}}};
            }
            if (const auto candidate = unrecognized_section_header(ev.comment_text)) {
                return db::Ignored{
                    {db::Diagnostic{beman::specgen::Severity::Warning,
                                    document_line(sm, doc, ev.offset) + candidate->line_offset,
                                    std::format("unrecognized section header [{}]; use \\rSec<depth>[{}]{{title}}",
                                                candidate->stable_name,
                                                candidate->stable_name)}}};
            }
            return db::Ignored{}; // \ref group headers, license/SPDX text, or
                                  // trailing braces (design §3.2): none of
                                  // these are structure, and none are errors.
        }
        return db::SectionOpen{ev.offset, header->value.depth, header->value.stable, header->value.title};
    }

    // A marked namespace-scope concept or variable is a
    // complete free-standing synopsis. The source spelling remains its IR
    // name; only the rendered declaration identifier takes the exposid name.
    //
    // A *defined* class template, or a specialization of one, is the
    // exception: it takes the ordinary class arms below, where extract_synopsis
    // gives it its exposid name and `// exposition only` tail. The standalone
    // arm copies the declaration's text through its semicolon, which for a
    // class definition kept every member body, every `\omit`ted member, and
    // every docblock and Doxygen comment in the class body, and collected none
    // of its members' own wording.
    const clang::NamedDecl* namespace_expos = nullptr;
    if (is_namespace_expos_candidate(ev.decl) && !is_defined_record(ev.decl))
        namespace_expos = llvm::cast<clang::NamedDecl>(ev.decl);
    if (namespace_expos != nullptr && expos_set.contains(namespace_expos->getCanonicalDecl())) {
        // The marker that brought it here is not the only one its docblock may
        // carry: bare `\seebelow` on a variable composes with `\expos` (issue
        // #38), and this arm used to read neither that nor the findings the
        // docblock's own parse produced.
        AttachedItem marked;
        attach_docblock(marked, ev.decl, sm);
        const VariableMask mask = variable_seebelow_mask(ev.decl, marked.directives, marked.grouping_line);
        if (mask.diagnostic)
            marked.diagnostics.push_back(*mask.diagnostic);
        // The kinds whose *definition* is the implementation mask it the same
        // way (issue #50): an alias's RHS, an alias template's, a concept's
        // constraint-expression. Bare `\seebelow` only -- the targeted forms
        // have no meaning here either, and variable_seebelow_mask has already
        // said so for the kinds it speaks for.
        std::optional<clang::SourceRange> see_below;
        if (marked.directives.seebelow && !marked.directives.seebelow_target)
            see_below = definition_mask_range(ev.decl, sm, lang_opts);

        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = namespace_expos->getNameAsString();
        out.synopsis.code = extract_namespace_expos_synopsis(
            namespace_expos, sm, lang_opts, ns_drop_set, expos_set, mask.unspecified, see_below);
        // Its description renders beside that synopsis, the way a class's own
        // description does (design §4.3): the draft describes exposition-only
        // concepts and aliases in prose -- "the exposition-only concept ... is
        // satisfied when". The docblock was parsed for the directives above and
        // its description then discarded, which dropped that wording silently.
        out.descr       = std::move(marked.item.descr);
        out.diagnostics = std::move(marked.diagnostics);
        return out;
    }

    // A documented namespace-scope alias (template), variable (template), or
    // concept is an ordinary wording item (design §6): the declaration
    // itself is the itemdecl and the docblock's description follows, the
    // way [concept.same] and [tuple.helper] write them. Marked `\expos` it
    // took the standalone-synopsis arm above instead (every marked kind here
    // is an expos candidate); undocumented it stays absent, like every other
    // unannotated entity. Aliases route through attach_alias, whose masking
    // rules (`\seebelow`/`\impdef`) and grouping apply at namespace scope
    // exactly as they do in a class body.
    if (const clang::NamedDecl* entity = as_namespace_entity(ev.decl);
        entity != nullptr && has_docblock(ev.decl, sm)) {
        if (const auto* alias_tmpl = llvm::dyn_cast<clang::TypeAliasTemplateDecl>(entity))
            return item_decl_event(
                attach_alias(alias_tmpl->getTemplatedDecl(), sm, lang_opts, ns_drop_set, expos_set, alias_tmpl), doc);
        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(entity))
            return item_decl_event(attach_alias(alias, sm, lang_opts, ns_drop_set, expos_set), doc);
        return item_decl_event(attach_namespace_entity(entity, sm, lang_opts, ns_drop_set, expos_set), doc);
    }

    // A class/struct/union definition heads a synopsis; every other top-level
    // decl (free function, out-of-line member definition, variable, ...) is a
    // spec item. A defined class gets its synopsis text extracted (design
    // §3.4); a declaration that defines nothing goes through
    // classify_record_declaration — an itemdecl when it is a documented
    // undefined primary, no node at all otherwise, and never an empty
    // Synopsis (which rendered as an empty code block).
    //
    // A class template is not itself a CXXRecordDecl — top-level iteration
    // yields the ClassTemplateDecl, whose templated decl (getTemplatedDecl())
    // is the CXXRecordDecl carrying the actual members — so it needs its own
    // arm or it would fall through to SpecItem and lose the whole class.
    // extract_synopsis takes the ClassTemplateDecl as the extraction range's
    // head so the synopsis starts at `template`, not at `class`.
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(ev.decl)) {
        if (!record->isThisDeclarationADefinition())
            return classify_record_declaration(record, record, sm, doc, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(record, sm))
            return db::Ignored{std::move(*diagnostics)};
        // An attached \verbatim-synopsis replaces the extracted synopsis text
        // (design §4.3, issue #4) — authored, span-free — while members,
        // roster, and derived class wording are collected exactly as for an
        // extracted one. The rest of the same docblock is the class's own
        // description, attached below.
        RecordDocblock   block = record_docblock(record, sm);
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = record->getNameAsString();
        out.diagnostics   = std::move(block.diagnostics);
        out.synopsis.code =
            block.synopsis
                ? ir::CodeText{std::move(*block.synopsis), {}}
                : extract_synopsis(
                      record, sm, lang_opts, omit_set, expos_set, seebelow_map, freestanding_map, ns_drop_set);
        collect_inclass_items(
            record, sm, lang_opts, ns_drop_set, expos_set, skipped, source_markup, out.pending, out.diagnostics);
        // After collection: the roster reads the routed sections
        // collect_inclass_items just decided.
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(record, sm, expos_set, source_markup, out.pending);
        group_adjacent_aliases(out.pending);
        attach_class_description(out, std::move(block), record, sm, lang_opts, ns_drop_set, expos_set);
        // After the description exists, and after grouping, which has no
        // opinion about it: `\at` on the class's own docblock sends both
        // halves of that wording to the section it names (issue #98).
        route_class_description(out, record, docblock_directives(ev.decl, sm));
        return out;
    }
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(ev.decl)) {
        const clang::CXXRecordDecl* templated = tmpl->getTemplatedDecl();
        if (templated == nullptr || !templated->isThisDeclarationADefinition())
            return classify_record_declaration(tmpl, templated, sm, doc, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(tmpl, sm))
            return db::Ignored{std::move(*diagnostics)};
        // Same attached-\verbatim-synopsis substitution, and the same class
        // description, as the plain-record arm above; the docblock is looked
        // up on the ClassTemplateDecl, the decl the arm's other docblock
        // reads use.
        RecordDocblock   block = record_docblock(tmpl, sm);
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = templated->getNameAsString();
        out.diagnostics   = std::move(block.diagnostics);
        out.synopsis.code = block.synopsis ? ir::CodeText{std::move(*block.synopsis), {}}
                                           : extract_synopsis(templated,
                                                              sm,
                                                              lang_opts,
                                                              omit_set,
                                                              expos_set,
                                                              seebelow_map,
                                                              freestanding_map,
                                                              ns_drop_set,
                                                              tmpl);
        collect_inclass_items(
            templated, sm, lang_opts, ns_drop_set, expos_set, skipped, source_markup, out.pending, out.diagnostics);
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(templated, sm, expos_set, source_markup, out.pending);
        group_adjacent_aliases(out.pending);
        attach_class_description(out, std::move(block), templated, sm, lang_opts, ns_drop_set, expos_set);
        // Same `\at` routing as the plain-record arm above, read from the
        // ClassTemplateDecl the arm's other docblock reads use.
        route_class_description(out, templated, docblock_directives(ev.decl, sm));
        return out;
    }

    // \also/\omit (design §4.3, Model A — see build_spec_item): an omitted
    // function definition contributes no node at all; a described overload
    // starts a group and an \also/empty-descr follower joins its signature
    // onto the group's primary instead of starting a new SpecItem. Only
    // function-definition items participate — a non-function decl (empty
    // SpecItem, is_function_def == false) can neither join nor act as a group
    // primary.
    // A namespace-scope function whose definition is in a source file (issue
    // #175): its item was built in that file's parse and sits here, at the
    // position of its declaration in this document, as if defined here.
    if (const clang::FunctionDecl* declared = declared_free_function(ev.decl)) {
        if (const auto sourced = source_markup.find(declared->getCanonicalDecl()); sourced != source_markup.end()) {
            AttachedItem attached   = sourced->second->attached;
            attached.inclass_offset = sm.getDecomposedLoc(ev.decl->getBeginLoc()).second;
            attached.file           = sm.getFileID(ev.decl->getBeginLoc());
            if (suppresses_generated_entity(attached.directives))
                return db::Ignored{std::move(attached.diagnostics)};
            return item_decl_event(std::move(attached), doc);
        }
    }

    AttachedItem attached = build_spec_item(ev.decl, sm, lang_opts, ns_drop_set, expos_set, skipped);
    if (!attached.is_function_def) {
        // A top-level decl no arm above turned into a node. Undocumented,
        // that is the ordinary fate of an unannotated entity (design §6),
        // and it stays silent. *Documented*, it means authored wording is
        // about to be dropped, and dropping it silently is the failure mode
        // a vocabulary header suffers worst — the header validates clean
        // while its descriptions vanish. An `\omit`/`\merge`/`\expos` marker
        // is the author asking for no itemdescr; anything else is an Error
        // naming the kind, so an unsupported entity kind is loud the first
        // time someone documents one.
        if (has_docblock(ev.decl, sm)) {
            const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(ev.decl, sm);
            if (!suppresses_generated_entity(dirs) && !dirs.expos) {
                const unsigned line = document_line(sm, doc, ev.offset);
                // A documented function *declaration* is the one shape here
                // with a better answer than "unsupported": the markup
                // belongs at the definition, which is what places a
                // function's wording (design §3.3). A deduction guide is a
                // FunctionDecl that can never have one, so it stays with the
                // unsupported-kind report.
                const clang::FunctionDecl* fn = as_out_of_line_function(ev.decl);
                std::string message = fn != nullptr && !llvm::isa<clang::CXXDeductionGuideDecl>(fn)
                                          ? "a documented function declaration produces no wording; markup belongs at "
                                            "the definition, which places a function's wording (design §3.3)"
                                          : std::format("a documented {} produces no wording: unsupported entity kind",
                                                        ev.decl->getDeclKindName());
                return db::Ignored{{db::Diagnostic{beman::specgen::Severity::Error,
                                                   line,
                                                   std::move(message),
                                                   diagnostic_file_name(sm, sm.getFileID(ev.decl->getBeginLoc()))}}};
            }
        }
        return db::Ignored{};
    }
    return item_decl_event(std::move(attached), doc);
}

} // namespace beman::specgen::frontend
