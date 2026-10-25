// src/beman/specgen/frontend/frontend.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/parse/parser.hpp>
#include <beman/specgen/foundation/fold_left_short.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/body_uses.hpp>
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/classify.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/directive_sets.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/foreign_names.hpp>
#include <beman/specgen/frontend/header_synopsis.hpp>
#include <beman/specgen/frontend/inclass_items.hpp>
#include <beman/specgen/frontend/parse_header.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/source_file.hpp>
#include <beman/specgen/frontend/source_files.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/rsec.hpp>
#include <beman/specgen/section_markers.hpp>

// GCC reports -Wnonnull eight times from inside LLVM's own headers when it
// inlines RecursiveASTVisitor::TraverseCXXRecordHelper for the visitors below:
// LazyOffsetPtr::get performs a pointer-to-member call on an
// ExternalASTSource* that is null whenever there is no PCH or module, which is
// exactly when the branch is not taken -- a precondition GCC cannot see
// through. -isystem does not silence it, because the diagnostic comes from the
// middle end after inlining rather than from the parse, so the suppression is
// scoped to the statements these headers contribute and our own code stays
// fully checked.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessingRecord.h>
#include <clang/Tooling/Core/Replacement.h>

#include <llvm/ADT/FoldingSet.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::frontend {

namespace foundation = beman::specgen::foundation;
namespace parse      = beman::specgen::parse;

using grammar::parse_ref;
using grammar::parse_rsec;
using grammar::section_end_fence;
using grammar::SectionEndFence;
using grammar::SectionHeader;

namespace {

namespace ir = beman::specgen::ir;

namespace db = beman::specgen::document_build;

} // namespace

std::expected<db::BuildResult, BuildFailure> build_document(std::string_view    header_path,
                                                            const ParseOptions& options) {
    // A projection of the richer entry point, not a second parse and not a
    // second implementation (see DocumentBuild in frontend.hpp): the callers
    // that want only the document say so by calling this.
    return build_document_with_sources(header_path, options).transform([](DocumentBuild built) {
        return std::move(built.result);
    });
}

std::expected<DocumentBuild, BuildFailure> build_document_with_sources(std::string_view               header_path,
                                                                       const ParseOptions&            options,
                                                                       const std::vector<SourceFile>& source_files) {
    namespace ir = beman::specgen::ir;

    ParsedHeader parsed = parse_header(header_path, options);
    if (!parsed.ast)
        return std::unexpected(unparsed_header(header_path));
    // Distinct from the message above, and deliberately so — Clang did
    // build an AST here, but its own DiagnosticsEngine says the parse that
    // produced it hit an error, which buildASTFromCodeWithArgs's recovery can
    // paper over with a non-null ASTUnit (ParsedHeader's doc comment above has
    // the mechanism). Reported as a BuildFailure rather than swallowed: this
    // pipeline's extraction is lexical, so a header Clang could not fully
    // parse does not fail loudly with a missing type — it fails quietly with
    // a plausible-looking wrong one (an unlinked
    // out-of-line definition renders `gadget::`-qualified and the roster calls
    // it undocumented), and `generate`'s output is wording a reader will trust.
    if (parsed.had_error)
        return std::unexpected(BuildFailure{std::format(
            "Clang reported an error parsing '{}'; the parse is incomplete, so the wording generated from it "
            "would be unreliable",
            header_path)});

    clang::ASTContext&        ctx       = parsed.ast->getASTContext();
    clang::SourceManager&     sm        = parsed.ast->getSourceManager();
    const clang::FileID       main_file = sm.getMainFileID();
    const clang::LangOptions& lang_opts = parsed.ast->getLangOpts();

    // Which files this document is: the main file, and the ones it `#include`s
    // inside a gathered `.syn` region (issue #77).  Everything below orders and
    // compares *document* offsets, which are the same numbers as before for a
    // document that is one file.
    const DocumentFiles doc = DocumentFiles::discover(*parsed.ast, sm);

    std::vector<clang::Decl*> decls;
    // substrate generic algorithm: same iterator-pair-turned-subrange,
    // for_each-shaped side effect as collect_interleaved's identical walk
    // above.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_top_level_decl(decl, sm, doc, decls);

    // Pre-passes: members to drop from every synopsis
    // (`\omit`/`\merge`) and exposition-only members (`\expos`) to render with an
    // `\exposid` span — both needed before the first extract_synopsis call below.
    // `\expos` alone reads a wider set than the document-structure walk
    // collected: the marked declaration may live in an included header while
    // the signature naming it is here (issue #36).
    std::vector<clang::Decl*> expos_decls = decls;
    // substrate generic algorithm: the same iterator-pair-turned-subrange,
    // for_each-shaped side effect as the walk just above.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_expos_scope_decl(decl, sm, main_file, expos_decls);

    std::set<const clang::Decl*>                    omit_set         = build_omit_set(decls, sm);
    const std::map<const clang::Decl*, std::string> expos_set        = build_expos_set(expos_decls, sm);
    const std::optional<std::vector<std::string>>   paper_entities   = collect_paper_entities(decls, sm);
    SeeBelowMap                                     seebelow_map     = build_seebelow_map(decls, sm);
    FreestandingMap                                 freestanding_map = build_freestanding_map(decls, sm);
    const std::set<std::string>                     ns_drop_set      = build_namespace_drop_set(decls);
    const SkippedRanges                             skipped          = collect_skipped_ranges(*parsed.ast);

    // The source files' items (issue #149), each built in its own parse and
    // matched here to the declaration it defines. Their markers act on the
    // header's synopsis just as a definition's in the header do, so they join
    // the same pre-pass sets.
    std::set<std::string> document_paths{canonical_file_path(sm, doc.main(), header_path)};
    document_paths.insert_range(doc.parts() | std::views::transform([&](const DocumentFiles::Part& part) {
                                    return canonical_file_path(sm, part.file, header_path);
                                }));
    // fold_left_short: a source file that fails stops the build, and each one
    // that parses merges its harvest into what the ones before it found.
    std::expected<SourceHarvest, BuildFailure> harvested = foundation::fold_left_short(
        source_files,
        SourceHarvest{},
        [&](SourceHarvest found, const SourceFile& source) -> std::expected<SourceHarvest, BuildFailure> {
            return harvest_source_file(source, document_paths).transform([&](SourceHarvest harvest) {
                found.items.merge(harvest.items);
                found.unextracted_uses.append_range(std::move(harvest.unextracted_uses));
                found.diagnostics.append_range(std::move(harvest.diagnostics));
                found.sources.append_range(std::move(harvest.sources));
                return found;
            });
        });
    if (!harvested)
        return std::unexpected(std::move(harvested.error()));
    std::map<DeclPosition, SourceMarkup>&                    source_items       = harvested->items;
    std::vector<beman::specgen::document_build::Diagnostic>& source_diagnostics = harvested->diagnostics;
    std::vector<std::string>&                                source_reads       = harvested->sources;
    std::vector<std::pair<std::string, std::string>>&        source_uses        = harvested->unextracted_uses;
    const SourceMarkupMap                                    source_markup =
        match_source_markup(decls, sm, header_path, source_items, source_diagnostics);
    std::ranges::for_each(source_markup, [&](const auto& entry) {
        const auto& [decl, markup] = entry;
        if (suppresses_generated_entity(markup->attached.directives))
            omit_set.insert(decl);
        if (const std::optional<SeeBelowTarget> target = seebelow_target(markup->attached.directives))
            seebelow_map.emplace(decl, *target);
        if (markup->attached.directives.freestanding_deleted)
            freestanding_map.emplace(decl, FreestandingKind::Deleted);
        else if (markup->attached.directives.freestanding)
            freestanding_map.emplace(decl, FreestandingKind::Freestanding);
    });

    // The docblock ranges some declaration consumed — top-level decls and
    // class members alike, since every comment in the file becomes a RawItem
    // below. classify() consults this for a comment carrying a verbatim
    // marker: attached, the declaration's own event substitutes the authored
    // text, and the comment's standalone node was the duplication (issue #4).
    AttachedCommentRanges attached_comments;
    const auto            note_attached_docblock = [&](const clang::Decl* decl) {
        const clang::RawComment* rc = attached_raw_comment(decl);
        if (rc == nullptr || !is_docblock_comment(rc->getRawText(sm)))
            return;
        const auto [begin_file, begin] = sm.getDecomposedLoc(rc->getBeginLoc());
        const auto [end_file, end]     = sm.getDecomposedLoc(rc->getEndLoc());
        if (begin_file == end_file && doc.contains(begin_file))
            attached_comments.emplace_back(doc.offset_in(begin_file, begin), doc.offset_in(end_file, end));
    };
    // substrate generic algorithm: a for_each-shaped walk driving
    // note_attached_docblock's side effect, over the same decls-plus-members
    // shape the build_omit_set pre-pass walks.
    for (const clang::Decl* decl : decls) {
        note_attached_docblock(decl);
        if (const clang::CXXRecordDecl* record = as_record_decl(decl);
            record != nullptr && record->isThisDeclarationADefinition())
            // substrate generic algorithm: the same side-effecting walk, one
            // level down — a class's members feed the same collector.
            for (const RealRecordMember& member : real_record_members(record))
                note_attached_docblock(member.decl);
    }

    std::vector<RawItem> raw_items;
    raw_items.reserve(decls.size());
    raw_items.append_range(decls | std::views::transform([&](clang::Decl* decl) {
                               return RawItem{doc.offset(sm, decl->getBeginLoc()), decl, {}};
                           }));
    const auto collect_comments = [&](clang::FileID file) {
        if (const std::map<unsigned, clang::RawComment*>* comments = ctx.Comments.getCommentsInFile(file)) {
            // substrate generic algorithm: one RawComment expands to one or
            // more RawItems, so this is a flat-map into the existing event
            // buffer.
            for (const auto& [offset, comment] : *comments)
                append_structure_comment_items(raw_items, doc.offset_in(file, offset), comment->getRawText(sm).str());
        }
    };
    collect_comments(doc.main());
    // substrate generic algorithm: a for_each-shaped walk driving
    // collect_comments' side effect over the followed files.
    for (const DocumentFiles::Part& part : doc.parts())
        collect_comments(part.file);
    // Same ordering contract as collect_interleaved (design §3.2).
    std::stable_sort(
        raw_items.begin(), raw_items.end(), [](const RawItem& a, const RawItem& b) { return a.offset < b.offset; });

    // The qualifiers ns_drop_set did *not* cover, collected before the
    // pipeline goes clang-free below and attached to the finished document.
    // It is document-level data, so it rides past build_tree rather than
    // through it — no DocEvent carries it and no node owns it.
    const std::vector<ir::ForeignNamespace> foreign = collect_foreign_namespaces(decls, ns_drop_set);

    // The bare-name complement (issue #84), collected the same way and for
    // the same reason: a name whose declaration this run never documents,
    // found by where the reference actually resolves rather than by what the
    // use site wrote.
    const std::vector<ir::ForeignDeclaration> foreign_decls = collect_foreign_declarations(decls, expos_set, sm, doc);

    // And the members named by the bodies this pipeline never renders,
    // collected here for the same reason and carried the same way — the one
    // thing design §9's leakage checker needs that no node can hold.
    const std::vector<ir::BodyUse> body_uses = collect_unextracted_uses(decls, sm);

    // A header synopsis is an ordinary .syn section whose source range
    // is closed by an exact Doxygen `/// END [stable]` line. Fold that bounded
    // range into one ordinary SynopsisDecl before the pipeline goes clang-free;
    // no header-shaped IR node or backend path is needed.
    std::vector<db::DocEvent> events;
    const auto                classify_one = [&](const RawItem& item) {
        return classify(item,
                        sm,
                        doc,
                        lang_opts,
                        omit_set,
                        expos_set,
                        seebelow_map,
                        freestanding_map,
                        ns_drop_set,
                        skipped,
                        attached_comments,
                        source_markup);
    };
    const auto boundary_diagnostic = [&](const RawItem& opener, std::string message) {
        const auto [file, local] = doc.locate(opener.offset);
        events.push_back(db::Ignored{{db::Diagnostic{beman::specgen::Severity::Warning,
                                                     sm.getLineNumber(file, local),
                                                     std::move(message),
                                                     diagnostic_file_name(sm, file)}}});
    };

    // substrate generic algorithm: a stateful source-order fold whose steps
    // consume either one ordinary item or a complete bounded synopsis range.
    for (std::size_t i = 0; i < raw_items.size();) {
        const RawItem&               opener = raw_items[i];
        std::optional<SectionHeader> header;
        if (opener.decl == nullptr)
            if (const auto parsed_header = parse_rsec(opener.comment_text))
                header = parsed_header->value;
        if (!header || !std::string_view(header->stable).ends_with(".syn")) {
            events.push_back(classify_one(opener));
            ++i;
            continue;
        }

        std::optional<std::size_t>     close_index;
        std::optional<SectionEndFence> close;
        std::optional<std::string>     boundary_error;
        // substrate generic algorithm: bounded lookahead for the first
        // structural conflict or exact matching END fence.
        for (std::size_t j = i + 1; j < raw_items.size(); ++j) {
            const RawItem& candidate = raw_items[j];
            if (candidate.decl != nullptr)
                continue;
            if (const auto nested = parse_rsec(candidate.comment_text); nested) {
                boundary_error = std::format(
                    "header synopsis [{}] is not closed before section [{}]", header->stable, nested->value.stable);
                break;
            }
            const auto end = section_end_fence(candidate.comment_text);
            if (!end)
                continue;
            if (end->stable != header->stable) {
                boundary_error =
                    std::format("header synopsis [{}] has mismatched END [{}]", header->stable, end->stable);
                break;
            }
            close_index = j;
            close       = end;
            break;
        }

        events.push_back(classify_one(opener));
        if (!close_index) {
            boundary_diagnostic(
                opener,
                boundary_error.value_or(std::format("header synopsis [{}] has no matching END", header->stable)));
            ++i;
            continue;
        }

        db::SynopsisDecl gathered;
        gathered.offset = opener.offset + 1;
        // The furthest end offset of any declaration this fold has consumed.
        // A comment before it sits *inside* that declaration's extracted
        // text — an in-class `\ref` group header, most commonly — and the
        // extraction already carries it, so appending it again as a
        // standalone group header rendered it twice (issue #31).
        unsigned consumed_end = 0;
        // What a folded-in declaration keeps that the gathered node cannot
        // hold. For a class: its class-general paragraph (design §5.2) and its
        // own description (issue #18), when an `\at` on the class has not
        // routed them out through `pending` already (issue #98) -- unrouted,
        // both belong beside their class's synopsis, in whatever section is
        // open, and the gathered node has one slot for each while a region may
        // hold several classes, so they travel on as their own events instead
        // of being merged (issue #41). For a namespace entity: its whole
        // wording, when nothing routes it elsewhere (issue #69). Pushed after
        // the gathered node, in source order, which is where their offsets
        // place them anyway.
        std::vector<db::DocEvent> extras;
        // The stable name of the `\ref` group header standing over the
        // declaration being folded, or empty. The in-class equivalent
        // (collect_inclass_items) reads the same headers out of the class
        // body; here the fold walks past them in source order, so the last one
        // seen is the one in force.
        std::string current_ref;
        // substrate generic algorithm: a source-order fold composing semantic
        // CodeText fragments while the outer cursor skips the consumed range.
        for (std::size_t j = i + 1; j <= *close_index; ++j) {
            RawItem item = raw_items[j];
            if (j == *close_index)
                item.comment_text.resize(close->line_begin);

            if (item.decl != nullptr) {
                const clang::SourceLocation decl_end_tok =
                    clang::Lexer::getLocForEndOfToken(item.decl->getEndLoc(), 0, sm, lang_opts);
                consumed_end                              = std::max(consumed_end, doc.offset(sm, decl_end_tok));
                const lowering::ItemDirectives directives = docblock_directives(item.decl, sm);
                db::DocEvent                   classified = classify_one(item);
                if (auto* synopsis = std::get_if<db::SynopsisDecl>(&classified);
                    synopsis != nullptr && !synopsis->synopsis.code.text.empty()) {
                    append_synopsis_code(gathered.synopsis.code, std::move(synopsis->synopsis.code));
                    gathered.diagnostics.append_range(std::move(synopsis->diagnostics));
                    // The class's routed in-class members ride along. They
                    // are not part of the gathered synopsis -- build_tree
                    // scatters each to the `\rSec` its `\ref` names, the
                    // same as for a class outside the region -- but the fold
                    // owns the only event they can travel on, and taking the
                    // code alone dropped them and their descriptions with it
                    // (issue #34), leaving the target section empty.
                    gathered.pending.append_range(std::move(synopsis->pending));
                    // And the coverage roster, which the gathered node used to
                    // carry none of: a class folded into a region was not
                    // coverage-checked at all, and `--validate` could not say
                    // so because the evidence never reached it (issue #45).
                    // Each entry names the class that declared it, so several
                    // classes' entries share this node without a finding
                    // losing track of whose member it is about.
                    gathered.synopsis.roster.append_range(std::move(synopsis->synopsis.roster));
                    // Only the class's own *unrouted* wording travels this
                    // way, in an event built for it rather than the classified
                    // one with its taken fields left behind: append_range over
                    // an rvalue container copies, so forwarding the whole event
                    // would scatter every routed member and report every
                    // finding a second time. An `\at` on the class emptied
                    // both fields into `pending` before the fold saw them, so
                    // this pushes nothing then and the wording rides the
                    // gathered node's own pending list to the section it named
                    // (issue #98). build_tree pushes no node for the synopsis
                    // itself, whose code really has been moved out.
                    if (synopsis->general || !synopsis->descr.elements.empty()) {
                        db::SynopsisDecl class_extra;
                        class_extra.offset  = synopsis->offset;
                        class_extra.general = std::move(synopsis->general);
                        class_extra.descr   = std::move(synopsis->descr);
                        extras.push_back(std::move(class_extra));
                    }
                } else {
                    if (auto* ignored = std::get_if<db::Ignored>(&classified))
                        gathered.diagnostics.append_range(std::move(ignored->diagnostics));
                    if (auto* item_decl = std::get_if<db::ItemDecl>(&classified)) {
                        // A misspelled tag is a typo whether or not the
                        // wording it belongs to finds a home, and this is the
                        // only place left to report it from.
                        gathered.diagnostics.append_range(std::move(item_decl->diagnostics));
                        // The region takes the declaration. It used to take
                        // the description with it and drop it (issue #69) --
                        // silently, and with no roster entry behind it, so
                        // coverage could not report the loss either. A
                        // described entity's wording travels instead, the way
                        // a folded-in class's members do: routed by `\at`,
                        // else by the `\ref` group header standing over it,
                        // to the section that should hold it. A range adaptor
                        // object is the case with no way around this -- a
                        // variable with an initializer has no out-of-line
                        // definition to carry its description into a later
                        // section, and moving its declaration out of the
                        // region would take it out of the header synopsis,
                        // where the draft puts it. Unrouted, the wording rides
                        // out beside the synopsis, which is where it lexically
                        // is. A declaration with nothing to say contributes
                        // only its declaration, exactly as before: a masked
                        // customization point object is the common case.
                        if (!item_decl->item.descr.elements.empty()) {
                            // An out-of-line member definition belongs to its
                            // class's group, not to whatever namespace-scope
                            // header was last seen (issue #77): in the house
                            // style the definition carries the wording, and
                            // its `\ref` group is written in the class body
                            // beside the declaration.
                            std::string inherited = current_ref;
                            if (const clang::FunctionDecl* fn = as_out_of_line_member(item.decl)) {
                                const clang::FunctionDecl* in_class = fn->getFirstDecl();
                                if (const auto* record =
                                        llvm::dyn_cast<clang::CXXRecordDecl>(in_class->getLexicalDeclContext()))
                                    inherited = ClassRefGroups(record, sm, lang_opts)
                                                    .section_for(sm.getDecomposedLoc(in_class->getBeginLoc()).second);
                            }
                            const std::string section = directives.at_anchor.value_or(inherited);
                            if (section.empty()) {
                                extras.push_back(std::move(*item_decl));
                            } else {
                                // The roster entry is what makes a route
                                // checkable: build_tree drops a pending item
                                // whose section no `\rSec` opens, and the
                                // entry is the only record that the request
                                // was made (§9's dangling-route rule, which
                                // reads exactly this). Without it a typo in an
                                // `\at` or a `\ref` header loses the wording
                                // as silently as not routing it at all did,
                                // and a namespace entity has no other roster
                                // entry to be caught by.
                                const auto* named = llvm::dyn_cast<clang::NamedDecl>(item.decl);
                                gathered.synopsis.roster.push_back(ir::SynopsisEntry{
                                    named != nullptr ? named->getNameAsString() : std::string{},
                                    ir::Disposition::Routed,
                                    section,
                                    llvm::isa<clang::FunctionDecl>(item.decl) ? ir::MemberKind::Function
                                                                              : ir::MemberKind::Data});
                                gathered.pending.push_back(
                                    db::PendingItem{section, item_decl->placement_key, std::move(item_decl->item)});
                            }
                        }
                    }
                    if (!suppresses_generated_entity(directives) && is_first_declaration(item.decl)) {
                        // The marker masks the declaration here exactly as it
                        // does outside the region (issue #55): a
                        // customization point object belongs in the header
                        // synopsis, so a masked variable is inside one by
                        // construction, and that is where the mask stopped
                        // being applied.
                        AttachedItem marked;
                        attach_docblock(marked, item.decl, sm);
                        const VariableMask mask =
                            variable_seebelow_mask(item.decl, marked.directives, marked.grouping_line);
                        std::optional<clang::SourceRange> see_below;
                        if (marked.directives.seebelow && !marked.directives.seebelow_target)
                            see_below = definition_mask_range(item.decl, sm, lang_opts);
                        append_synopsis_code(
                            gathered.synopsis.code,
                            extract_header_declaration(
                                item.decl, sm, lang_opts, ns_drop_set, expos_set, mask.unspecified, see_below));
                    }
                }
                continue;
            }
            if (item.comment_text.empty() || item.offset < consumed_end)
                continue;

            db::DocEvent classified = classify_one(item);
            if (auto* synopsis = std::get_if<db::SynopsisDecl>(&classified)) {
                append_synopsis_code(gathered.synopsis.code, std::move(synopsis->synopsis.code));
                gathered.diagnostics.append_range(std::move(synopsis->diagnostics));
                continue;
            }

            // Of ordinary comments, only draft synopsis group headers are
            // semantic here. Markup, Doxygen, and namespace-closing comments
            // are source scaffolding and do not enter the gathered node.
            std::string refs;
            std::size_t line_begin = 0;
            // substrate generic algorithm: scan physical lines so only exact
            // draft Ref headers survive from a possibly merged RawComment.
            while (line_begin <= item.comment_text.size()) {
                const std::size_t      newline  = item.comment_text.find('\n', line_begin);
                const std::size_t      line_end = newline == std::string::npos ? item.comment_text.size() : newline;
                const std::string_view line(item.comment_text.data() + line_begin, line_end - line_begin);
                if (line_vocabulary(llvm::StringRef{line}.ltrim(" \t")) == CommentVocabulary::Draft) {
                    if (const std::optional<std::string> ref = parse_ref(line)) {
                        current_ref = *ref;
                        if (!refs.empty())
                            refs.push_back('\n');
                        refs.append(line);
                    }
                }
                if (newline == std::string::npos)
                    break;
                line_begin = newline + 1;
            }
            append_synopsis_code(gathered.synopsis.code, header_comment_code(std::move(refs)));
        }
        events.push_back(std::move(gathered));
        events.append_range(std::move(extras));
        i = *close_index + 1;
    }

    // Stages 2-3: build_tree() folds the `\rSec` frame stack into a tree,
    // grouping each frame's own `\also`/empty-descr followers onto their
    // primaries (via group_items()) in push order as that frame closes —
    // see document_build.hpp's top-of-file note for why stage 3 runs there
    // rather than as a separate pass over the finished tree. Both clang-free,
    // both unit-tested with synthetic events in
    // tests/beman/specgen/document_build.test.cpp.
    // Last, so a gathered region is enclosed once, as a whole, rather than
    // once per class folded into it.
    std::ranges::for_each(events, [](db::DocEvent& event) {
        if (auto* synopsis = std::get_if<db::SynopsisDecl>(&event); synopsis != nullptr && !synopsis->exact)
            enclose_in_namespace_std(synopsis->synopsis.code);
    });

    db::BuildResult built               = db::build_tree(events);
    built.document.paper_entities       = paper_entities;
    built.document.foreign_namespaces   = foreign;
    built.document.foreign_declarations = foreign_decls;
    built.document.unextracted_uses     = body_uses;
    // A source file's bodies join the header's, sorted and deduplicated as
    // the header's own are (issue #176).
    built.document.unextracted_uses.append_range(
        source_uses | std::views::transform([](const auto& use) { return ir::BodyUse{use.first, use.second}; }));
    std::ranges::sort(built.document.unextracted_uses, [](const ir::BodyUse& a, const ir::BodyUse& b) {
        return std::tie(a.function, a.member) < std::tie(b.function, b.member);
    });
    const auto duplicates =
        std::ranges::unique(built.document.unextracted_uses, [](const ir::BodyUse& a, const ir::BodyUse& b) {
            return a.function == b.function && a.member == b.member;
        });
    built.document.unextracted_uses.erase(duplicates.begin(), duplicates.end());
    built.diagnostics.insert(built.diagnostics.begin(), source_diagnostics.begin(), source_diagnostics.end());
    std::vector<std::string> sources = parse_sources(header_path, *parsed.ast);
    sources.append_range(std::move(source_reads));
    return DocumentBuild{std::move(built), std::move(sources)};
}

} // namespace beman::specgen::frontend
