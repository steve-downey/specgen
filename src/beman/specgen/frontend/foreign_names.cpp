// src/beman/specgen/frontend/foreign_names.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/foreign_names.hpp>

#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>

#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Path.h>

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// Names intentionally supplied by the paper but absent from this document's
// rendered nodes (issue #113). `\elsewhere` is the explicit promise that the
// paper hand-authors a declaration which specgen must suppress. A `\expos`
// namespace using-declaration has no standalone declaration to render, but it
// likewise makes its introduced name available to the paper and marks its
// target for exposition-use rewriting in build_expos_set above.
class PaperEntityCollector : public clang::RecursiveASTVisitor<PaperEntityCollector> {
  public:
    PaperEntityCollector(const clang::SourceManager& sm, std::set<std::string>& out) : sm_(sm), out_(out) {}

    bool VisitNamedDecl(clang::NamedDecl* decl) {
        if (decl == nullptr || decl->isImplicit())
            return true;
        const beman::specgen::lowering::ItemDirectives directives = docblock_directives(decl, sm_);
        if (directives.elsewhere || (directives.expos && llvm::isa<clang::UsingDecl>(decl)))
            if (const std::string name = decl->getNameAsString(); !name.empty())
                out_.insert(name);
        return true;
    }

  private:
    const clang::SourceManager& sm_;
    std::set<std::string>&      out_;
};

// The complement of QualifierDropper's walk. That visitor answers "does
// this qualifier resolve into the drop set?" and, when it does, deletes it;
// the *no* answers are what design §9's leakage checker needs, and this
// visitor is what keeps them. A qualifier the drop set does not cover is left
// verbatim in rendered code, and a reader of the wording cannot see what
// `detail::` names — so the resolution the dropper already performs is
// exactly the resolution the check needs.
//
// Recorded per *namespace*, not per occurrence: the IR channel is
// document-level (ir::ForeignNamespace) and the validator locates occurrences
// by matching the name against rendered text, the same way its roster half
// locates a leaked member. That makes over-collection harmless — a qualifier
// written only inside a body the tool never renders contributes a name that
// matches nothing — which is why this walks every main-file decl rather than
// threading a second output through the six places QualifierDropper runs.
//
// Two guards keep the finding one-directional (a miss, never a false one):
//
//   - a namespace already in the drop set is skipped even when it is part of a
//     surviving chain, so `demo::detail::storage` reports `detail` alone;
//   - a namespace rooted in `std` is skipped. A `std::ranges::` qualifier is
//     the case this exists for: it survives the drop set (the set holds
//     `std`, not `std::ranges`) and belongs in the draft verbatim, because it
//     is the standard's own vocabulary. That is a property of the namespace's
//     qualified path, not of where it happens to be declared — the guard used
//     to be "declared in the main file", which let the identical rendered
//     `detail::` pass unreported the moment the helper namespace moved into
//     an included implementation header, disabling the leakage guarantee by
//     exactly the refactor the tool's own guidance encourages.
class ForeignQualifierCollector : public clang::RecursiveASTVisitor<ForeignQualifierCollector> {
  public:
    ForeignQualifierCollector(const std::set<std::string>& drop, std::map<std::string, std::string>& out)
        : drop_(drop), out_(out) {}

    bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc nns) {
        if (nns) {
            const std::optional<std::string> written = qualifier_namespace_name(nns.getNestedNameSpecifier());
            if (written && drop_.count(*written) == 0)
                record(nns.getNestedNameSpecifier());
        }
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseNestedNameSpecifierLoc(nns);
    }

    bool TraverseDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr && imported_qualifier_is_droppable(expr->getDecl(), expr->getQualifierLoc(), drop_))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseDeclRefExpr(expr);
    }

    bool TraverseConceptSpecializationExpr(clang::ConceptSpecializationExpr* expr) {
        if (expr != nullptr &&
            imported_qualifier_is_droppable(expr->getNamedConcept(), expr->getNestedNameSpecifierLoc(), drop_))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseConceptSpecializationExpr(expr);
    }

    bool TraverseUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr && expr->getQualifierLoc() && expr->decls_begin() != expr->decls_end() &&
            std::ranges::all_of(expr->decls(), [&](clang::NamedDecl* decl) {
                return imported_qualifier_is_droppable(decl, expr->getQualifierLoc(), drop_);
            }))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseUnresolvedLookupExpr(expr);
    }

  private:
    // Each namespace the surviving qualifier writes, innermost first — the
    // same unfold qualifier_namespace_name makes, kept separate because that
    // one joins the chain into the single string the drop set is keyed on and
    // this one needs each link's decl.
    void record(clang::NestedNameSpecifier qualifier) {
        // substrate generic algorithm: the same NestedNameSpecifier unfold
        // qualifier_namespace_name performs (this file, step 07) -- each
        // qualifier yields only its own prefix, so the chain does not exist
        // as a range until the walk produces it.
        while (qualifier.getKind() == clang::NestedNameSpecifier::Kind::Namespace) {
            const clang::NamespaceAndPrefix np    = qualifier.getAsNamespaceAndPrefix();
            const auto*                     named = llvm::dyn_cast_or_null<clang::NamedDecl>(np.Namespace);
            if (named == nullptr)
                return;
            const std::string name      = named->getNameAsString();
            const std::string qualified = named->getQualifiedNameAsString();
            if (!name.empty() && drop_.count(qualified) == 0 && !qualified.starts_with("std::"))
                out_.emplace(name, qualified);
            qualifier = np.Prefix;
        }
    }

    const std::set<std::string>&        drop_;
    std::map<std::string, std::string>& out_;
};

// `ForeignQualifierCollector`'s complement for a name with no qualifier at
// all (issue #84): a helper sharing the header's own namespace, so nothing
// written at the use site ever names a foreign namespace, needs a check keyed
// on where the *referenced declaration* lives rather than on what the use
// site wrote. Same AST-node coverage `ExposUseFinder` reads a name from
// (`DeclRefExpr`, a concept reference, an unresolved lookup's candidates, and
// the three exposition-only type-use shapes) -- reused here for the opposite
// question: not "is this decl `\expos`-marked", but "is this decl invisible
// to the reader for a reason `\expos` was never asked to fix".
//
// Two guards keep this one-directional the same way `ForeignQualifierCollector`
// is:
//
//   - a decl already in `expos_` is skipped: it renders under its
//     `\exposid` sentinel, which is design §9's own escape hatch, and the
//     fix for a name this check *would* otherwise flag is exactly to mark it
//     `\expos` where it is actually declared -- `\expos` already reaches an
//     included, non-system header (issue #36), so no separate "trust me"
//     marker is needed;
//   - a decl in a system header is skipped: nothing under one carries specgen
//     markup, and a system header is the standard's own vocabulary, the same
//     boundary `collect_expos_scope_decl` already draws.
//
// A decl `DocumentFiles::contains` (this run's main file, or one of its
// gathered `.syn` follows, issue #77) is not "foreign" by construction: it is
// either documented through the ordinary path or reported by some other §9
// check (undocumented, `\omit`ted, private) -- this check is only about a
// declaration this run never had a chance to say anything about at all.
class ForeignDeclCollector : public clang::RecursiveASTVisitor<ForeignDeclCollector> {
  public:
    ForeignDeclCollector(const std::map<const clang::Decl*, std::string>& expos,
                         const clang::SourceManager&                      sm,
                         const DocumentFiles&                             doc,
                         std::map<std::string, std::string>&              out)
        : expos_(expos), sm_(sm), doc_(doc), out_(out) {}

    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr)
            record(expr->getDecl());
        return true;
    }

    bool VisitConceptReference(clang::ConceptReference* ref) {
        if (ref != nullptr)
            record(ref->getNamedConcept());
        return true;
    }

    bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr)
            for (clang::NamedDecl* candidate : expr->decls()) // substrate generic algorithm
                record(candidate);
        return true;
    }

    bool VisitTypedefTypeLoc(clang::TypedefTypeLoc tl) {
        record(tl.getDecl());
        return true;
    }

    bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc tl) {
        record(tl.getTypePtr()->getTemplateName().getAsTemplateDecl());
        return true;
    }

    bool VisitTagTypeLoc(clang::TagTypeLoc tl) {
        record(tl.getDecl());
        return true;
    }

    // The shadow, as ExposUseFinder looks it up: one that is not exposition-
    // only still names its target, and the target is what may be foreign.
    bool VisitUsingTypeLoc(clang::UsingTypeLoc tl) {
        const clang::UsingShadowDecl* shadow = tl.getDecl();
        if (shadow != nullptr && !expos_.contains(shadow->getCanonicalDecl()))
            record(shadow->getTargetDecl());
        return true;
    }

  private:
    void record(const clang::NamedDecl* decl) {
        if (decl == nullptr)
            return;
        const clang::Decl* canonical = decl->getCanonicalDecl();
        if (const auto* specialization = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
            canonical = specialization->getSpecializedTemplate()->getCanonicalDecl();
        if (expos_.contains(canonical))
            return;
        // An enumerator is named through its enumeration: one of an
        // exposition-only enumeration is as visible as the enumeration is,
        // wherever that enumeration happens to be declared.
        if (const auto* enumerator = llvm::dyn_cast<clang::EnumConstantDecl>(decl);
            enumerator != nullptr &&
            expos_.contains(llvm::cast<clang::Decl>(enumerator->getDeclContext())->getCanonicalDecl()))
            return;
        const clang::SourceLocation loc = decl->getLocation();
        if (!loc.isValid() || sm_.isInSystemHeader(loc) || doc_.contains(sm_.getDecomposedLoc(loc).first))
            return;
        const std::string name = decl->getNameAsString();
        // Bare filename, not `diagnostic_file_name`'s path-relative-to-the-
        // main-file convention: that convention assumes the file it names
        // moves in lockstep with the main file (true for a `.syn` follow,
        // which is fixed relative to it by the very `#include` that pulled it
        // in), and this decl's declaring header is under no such constraint
        // -- reached by an arbitrary `-I` or quoted-relative search, so a
        // main file relocated on its own (as the east-const golden's flipped
        // copy is) would report a different relative path for the identical
        // fact.
        if (!name.empty())
            out_.emplace(name, llvm::sys::path::filename(sm_.getFilename(loc)).str());
    }

    const std::map<const clang::Decl*, std::string>& expos_;
    const clang::SourceManager&                      sm_;
    const DocumentFiles&                             doc_;
    std::map<std::string, std::string>&              out_;
};

// Every name the document itself declares, at any depth (decision
// shared-spelling-foreign-name). The check `ForeignDeclCollector` feeds
// *resolves* a reference but *reports* it by text, and a spelling can name
// two entities: a generated `detail::tables::windows_1252` and the
// `codec::windows_1252` enumerator it is the table for are the same word and
// different declarations, deliberately, because both are named after the
// encoding. Text-matching the foreign one then fires at every occurrence of
// the documented one -- starting with the enumeration's own declaration
// (issue #93), where the check reports an enumerator as undocumented at the
// point where the enumeration that documents it is being rendered.
//
// So this is deliberately wider than the roster's `documented` set the
// validator already consults, which is what let the report through: a roster
// records the enumeration, never its enumerators, so the run genuinely did
// not know it declared that spelling. It is wider at the other end too -- a
// private or `\omit`ted member's name suppresses the foreign report as well.
// That is the direction to be wrong in. A name this run declares is this
// run's to explain, and whether it explains it well is a question every
// other design §9 check is *already* asking about that same declaration, by
// disposition rather than by text; a foreign entity sharing its spelling is
// evidence of nothing, and reporting it costs the author an `\expos` that
// asserts something false about a header the document never reaches.
//
// Locations are checked, not decl contexts: a declaration written in the
// document is the document's whatever its scope -- a class member, an
// enumerator, a template parameter, a local of a body that never becomes
// wording.
class DocumentNameCollector : public clang::RecursiveASTVisitor<DocumentNameCollector> {
  public:
    DocumentNameCollector(const clang::SourceManager& sm, const DocumentFiles& doc, std::set<std::string>& out)
        : sm_(sm), doc_(doc), out_(out) {}

    bool VisitNamedDecl(clang::NamedDecl* decl) {
        // Implicit declarations are compiler synthesis, not source, the same
        // exclusion collect_top_level_decl draws: an implicit member reports
        // its class's own location, so it would contribute a name the author
        // never wrote at a place the author never wrote it.
        if (decl == nullptr || decl->isImplicit())
            return true;
        const clang::SourceLocation loc = decl->getLocation();
        if (!loc.isValid() || !doc_.contains(sm_.getDecomposedLoc(loc).first))
            return true;
        if (const std::string name = decl->getNameAsString(); !name.empty())
            out_.insert(name);
        return true;
    }

  private:
    const clang::SourceManager& sm_;
    const DocumentFiles&        doc_;
    std::set<std::string>&      out_;
};

} // namespace

std::optional<std::vector<std::string>> collect_paper_entities(const std::vector<clang::Decl*>& decls,
                                                               const clang::SourceManager&      sm) {
    std::set<std::string> declared;
    PaperEntityCollector  collector(sm, declared);
    // substrate generic algorithm: a for_each-shaped RecursiveASTVisitor walk.
    for (clang::Decl* decl : decls)
        collector.TraverseDecl(decl);
    std::vector<std::string> entities(declared.begin(), declared.end());
    if (entities.empty())
        return std::nullopt;
    return entities;
}

std::vector<beman::specgen::ir::ForeignNamespace> collect_foreign_namespaces(const std::vector<clang::Decl*>& decls,
                                                                             const std::set<std::string>&     drop) {
    // The map deduplicates and orders by name, so the IR channel is stable
    // whichever decl wrote the qualifier first.
    std::map<std::string, std::string> found;
    ForeignQualifierCollector          collector(drop, found);
    // substrate generic algorithm: a for_each-shaped walk driving
    // RecursiveASTVisitor's side effect (appending to `found`), the same
    // shape build_document's collect_top_level_decl loop has -- not a
    // transform building a container from a range.
    for (clang::Decl* decl : decls)
        collector.TraverseDecl(decl);
    return found | std::views::transform([](const auto& kv) {
               return beman::specgen::ir::ForeignNamespace{kv.first, kv.second};
           }) |
           std::ranges::to<std::vector>();
}

std::vector<beman::specgen::ir::ForeignDeclaration>
collect_foreign_declarations(const std::vector<clang::Decl*>&                 decls,
                             const std::map<const clang::Decl*, std::string>& expos,
                             const clang::SourceManager&                      sm,
                             const DocumentFiles&                             doc) {
    std::map<std::string, std::string> found;
    ForeignDeclCollector               collector(expos, sm, doc, found);
    for (clang::Decl* decl : decls) // substrate generic algorithm
        collector.TraverseDecl(decl);

    // The same walk over the same declarations, asking the other question:
    // which spellings are the document's own. Two passes rather than one
    // visitor answering both, because the two sets are keyed on opposite
    // ends of a reference -- where a *referenced* declaration lives, and
    // where a *written* declaration lives -- and only their names ever meet.
    std::set<std::string> declared;
    DocumentNameCollector names(sm, doc, declared);
    for (clang::Decl* decl : decls) // substrate generic algorithm
        names.TraverseDecl(decl);

    return found | std::views::filter([&declared](const auto& kv) { return !declared.contains(kv.first); }) |
           std::views::transform(
               [](const auto& kv) { return beman::specgen::ir::ForeignDeclaration{kv.first, kv.second}; }) |
           std::ranges::to<std::vector>();
}

} // namespace beman::specgen::frontend
