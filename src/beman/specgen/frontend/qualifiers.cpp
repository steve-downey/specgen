// src/beman/specgen/frontend/qualifiers.cpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/qualifiers.hpp>

#include <clang/AST/ASTConcept.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <concepts>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// The namespace that actually owns a referenced declaration. A using-
// declaration can make a std entity reachable through an implementation
// namespace, so this can intentionally differ from the qualifier written at
// the use site.
std::optional<std::string> declaration_namespace_name(const clang::NamedDecl* decl) {
    if (decl == nullptr)
        return std::nullopt;
    decl = decl->getUnderlyingDecl();
    if (const auto* specialization = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
        decl = specialization->getSpecializedTemplate();

    std::vector<std::string> parts;
    // substrate generic algorithm: unfold the declaration-context ancestry;
    // the next context is carried by the current node rather than an iterator.
    for (const clang::DeclContext* context = decl->getDeclContext(); context != nullptr;
         context                           = context->getParent()) {
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(context);
            ns != nullptr && !ns->isAnonymousNamespace())
            parts.push_back(ns->getNameAsString());
    }
    if (parts.empty())
        return std::nullopt;
    std::reverse(parts.begin(), parts.end());
    return parts | std::views::join_with(std::string_view("::")) | std::ranges::to<std::string>();
}

// Collects the source ranges of droppable namespace qualifiers under a decl.
// RecursiveASTVisitor in this LLVM has no VisitNestedNameSpecifierLoc hook, so
// the traversal method itself is overridden.
class QualifierDropper : public clang::RecursiveASTVisitor<QualifierDropper> {
  public:
    QualifierDropper(const std::set<std::string>&                drop,
                     const clang::SourceManager&                 sm,
                     const clang::LangOptions&                   lang_opts,
                     std::vector<std::pair<unsigned, unsigned>>& out)
        : drop_(drop), sm_(sm), lang_opts_(lang_opts), out_(out) {}

    // RecursiveASTVisitor does not descend into a constructor's
    // explicit-specifier expression, so `explicit(std::is_convertible_v<U, T>)`
    // would keep its qualifier while the requires-clause beside it lost one.
    // Traverse it explicitly.
    bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl* ctor) {
        if (ctor != nullptr) {
            if (const clang::Expr* expr = ctor->getExplicitSpecifier().getExpr())
                TraverseStmt(const_cast<clang::Expr*>(expr));
        }
        return clang::RecursiveASTVisitor<QualifierDropper>::TraverseCXXConstructorDecl(ctor);
    }

    bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc nns) {
        if (nns) {
            const std::optional<std::string> name = qualifier_namespace_name(nns.getNestedNameSpecifier());
            if (name && drop_.count(*name) != 0) {
                const clang::SourceRange range = nns.getSourceRange();
                if (range.isValid() && range.getBegin().isFileID() && range.getEnd().isFileID()) {
                    const unsigned begin = sm_.getDecomposedLoc(range.getBegin()).second;
                    const unsigned end =
                        sm_.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm_, lang_opts_))
                            .second;
                    if (end > begin)
                        out_.emplace_back(begin, end);
                }
                return true; // the whole qualifier goes; do not descend into its prefix
            }
        }
        return clang::RecursiveASTVisitor<QualifierDropper>::TraverseNestedNameSpecifierLoc(nns);
    }

    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr)
            add_imported(expr->getDecl(), expr->getQualifierLoc());
        return true;
    }

    bool VisitConceptSpecializationExpr(clang::ConceptSpecializationExpr* expr) {
        if (expr != nullptr)
            add_imported(expr->getNamedConcept(), expr->getNestedNameSpecifierLoc());
        return true;
    }

    bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr == nullptr || !expr->getQualifierLoc())
            return true;
        const bool all_imported = expr->decls_begin() != expr->decls_end() &&
                                  std::ranges::all_of(expr->decls(), [&](clang::NamedDecl* decl) {
                                      return imported_qualifier_is_droppable(decl, expr->getQualifierLoc(), drop_);
                                  });
        if (all_imported)
            add_range(expr->getQualifierLoc());
        return true;
    }

  private:
    void add_imported(const clang::NamedDecl* decl, clang::NestedNameSpecifierLoc qualifier) {
        if (imported_qualifier_is_droppable(decl, qualifier, drop_))
            add_range(qualifier);
    }

    void add_range(clang::NestedNameSpecifierLoc qualifier) {
        const clang::SourceRange range = qualifier.getSourceRange();
        if (!range.isValid() || !range.getBegin().isFileID() || !range.getEnd().isFileID())
            return;
        const unsigned begin = sm_.getDecomposedLoc(range.getBegin()).second;
        const unsigned end =
            sm_.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm_, lang_opts_)).second;
        if (end > begin)
            out_.emplace_back(begin, end);
    }

    const std::set<std::string>&                drop_;
    const clang::SourceManager&                 sm_;
    const clang::LangOptions&                   lang_opts_;
    std::vector<std::pair<unsigned, unsigned>>& out_;
};

// Reference-resolved uses of namespace-scope exposition-only declarations.
// Each use carries its own qualifier range: marking detail::helper does not
// make the detail namespace globally visible or globally droppable.
class ExposUseFinder : public clang::RecursiveASTVisitor<ExposUseFinder> {
  public:
    ExposUseFinder(const std::map<const clang::Decl*, std::string>& expos,
                   const clang::SourceManager&                      sm,
                   const clang::LangOptions&                        lang_opts,
                   std::vector<ExposUse>&                           out)
        : expos_(expos), sm_(sm), lang_opts_(lang_opts), out_(out) {}

    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr)
            add(expr->getDecl(), expr->getNameInfo().getSourceRange(), expr->getQualifierLoc());
        return true;
    }

    // Every written mention of a concept, whichever spelling wrote it: the
    // `C<T>` of a requires-clause holds one of these, and so does the `C T` of
    // a constrained template parameter, where there is no
    // ConceptSpecializationExpr to visit at all -- the shorthand's constraint
    // hangs off the TemplateTypeParmDecl (issue #48). Visiting the reference
    // rather than the expression covers both from one hook, and covers each
    // exactly once.
    bool VisitConceptReference(clang::ConceptReference* ref) {
        if (ref != nullptr)
            add(ref->getNamedConcept(), ref->getConceptNameInfo().getSourceRange(), ref->getNestedNameSpecifierLoc());
        return true;
    }

    bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr) {
            // A dependent variable-template-id retains lookup candidates
            // rather than becoming a DeclRefExpr until instantiation.
            for (clang::NamedDecl* candidate : expr->decls()) { // substrate generic algorithm
                add(candidate, expr->getNameInfo().getSourceRange(), expr->getQualifierLoc());
            }
        }
        return true;
    }

    // Type uses, for the exposition-only alias kinds: a plain alias is
    // written as a TypedefTypeLoc and an alias-template-id as a
    // TemplateSpecializationTypeLoc whose template name resolves to the
    // TypeAliasTemplateDecl — the same canonical decl the expos set keys.
    // Both carry their own qualifier loc on this LLVM, so a qualified
    // `detail::traverse_context_t<int>` drops its qualifier per use exactly
    // like the expression cases above.
    bool VisitTypedefTypeLoc(clang::TypedefTypeLoc tl) {
        add(tl.getDecl(), clang::SourceRange(tl.getNameLoc()), tl.getQualifierLoc());
        return true;
    }

    bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc tl) {
        add(tl.getTypePtr()->getTemplateName().getAsTemplateDecl(),
            clang::SourceRange(tl.getTemplateNameLoc()),
            tl.getQualifierLoc());
        return true;
    }

    // An exposition-only enumeration or plain class is written as a
    // TagTypeLoc -- the type of a member, a parameter, or a variable (issues
    // #68, #142). Without this hook its own declaration rendered under its
    // exposid name while every use kept the real one, which is the wording
    // naming an entity the reader cannot see. A class template's uses are
    // template-ids, handled above.
    bool VisitTagTypeLoc(clang::TagTypeLoc tl) {
        add(tl.getDecl(), clang::SourceRange(tl.getNameLoc()), tl.getQualifierLoc());
        return true;
    }

    // A type named through a using-declaration -- `detail::helper_t` after
    // `namespace detail { using impl::helper_t; }` -- is a UsingTypeLoc whose
    // decl is the shadow, and nothing beneath it is traversed. An `\expos`
    // using-declaration puts its shadows in the set beside their targets, so
    // the shadow is what this looks up. Templates never needed it: their
    // TemplateName looks through the shadow to the template (issue #142).
    bool VisitUsingTypeLoc(clang::UsingTypeLoc tl) {
        add(tl.getDecl(), clang::SourceRange(tl.getNameLoc()), tl.getQualifierLoc());
        return true;
    }

  private:
    void add(const clang::NamedDecl* decl, clang::SourceRange name_range, clang::NestedNameSpecifierLoc qualifier) {
        if (decl == nullptr)
            return;
        const clang::NamedDecl* marked_decl = decl;
        if (const auto* specialization = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
            marked_decl = specialization->getSpecializedTemplate();
        const auto found = expos_.find(marked_decl->getCanonicalDecl());
        if (found == expos_.end() || !name_range.isValid())
            return;

        const auto offset_after = [&](clang::SourceLocation loc) {
            return sm_.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(loc, 0, sm_, lang_opts_)).second;
        };
        const unsigned name_begin = sm_.getDecomposedLoc(name_range.getBegin()).second;
        const unsigned name_end   = offset_after(name_range.getEnd());
        unsigned       qual_begin = name_begin;
        unsigned       qual_end   = name_begin;
        if (qualifier) {
            const clang::SourceRange range = qualifier.getSourceRange();
            if (range.isValid()) {
                qual_begin = sm_.getDecomposedLoc(range.getBegin()).second;
                qual_end   = offset_after(range.getEnd());
            }
        }
        out_.push_back(ExposUse{
            name_begin, name_end, qual_begin, qual_end, found->second, llvm::isa<clang::ParmVarDecl>(marked_decl)});
    }

    const std::map<const clang::Decl*, std::string>& expos_;
    const clang::SourceManager&                      sm_;
    const clang::LangOptions&                        lang_opts_;
    std::vector<ExposUse>&                           out_;
};

template <class AstNode>
std::vector<ExposUse> find_expos_uses(AstNode*                                         root,
                                      const std::map<const clang::Decl*, std::string>& expos,
                                      const clang::SourceManager&                      sm,
                                      const clang::LangOptions&                        lang_opts) {
    std::vector<ExposUse> out;
    if (root != nullptr && !expos.empty()) {
        ExposUseFinder finder(expos, sm, lang_opts, out);
        if constexpr (std::derived_from<AstNode, clang::Decl>)
            finder.TraverseDecl(root);
        else
            finder.TraverseStmt(root);
    }
    return out;
}

// A use of an exposition-only entity deletes its *whole* qualifier, so a
// namespace-qualifier edit nested inside that range has nothing left to do --
// and doing it is worse than redundant. The apply loops scatter edits in
// descending offset order and skip anything inside a range already consumed
// (design 3.4), so the nested edit is taken first and its watermark then
// suppresses the deletion it sits inside. The name replacement is a separate,
// non-overlapping range and still lands, which is how `detail::` survives
// beside an exposid that was supposed to take it along.
//
// The shape that reaches this is a qualifier whose head is droppable and whose
// whole is not: `beman::transcoding::detail::` written by a specialization
// declared in another namespace, where writing it relative to the specified
// namespace is not an option. Written as `detail::` from inside, there is no
// prefix to strip, no nested edit, and the deletion has always worked -- which
// is how one entity came to render two ways in one synopsis.
//
// Only exposition-only uses are filtered. A qualifier on anything else is the
// leakage checker's business, and `std::ranges::` must still lose its `std::`.
void drop_edits_inside_expos_qualifiers(std::vector<std::pair<unsigned, unsigned>>& edits,
                                        const std::vector<ExposUse>&                uses) {
    std::erase_if(edits, [&uses](const std::pair<unsigned, unsigned>& edit) {
        // substrate generic algorithm: a containment search over the uses, not
        // an index into them -- the qualifier ranges do not nest in each other.
        return std::ranges::any_of(uses, [&edit](const ExposUse& use) {
            return use.qualifier_end > use.qualifier_begin && edit.first >= use.qualifier_begin &&
                   edit.second <= use.qualifier_end;
        });
    });
}

} // namespace

std::optional<std::string> qualifier_namespace_name(clang::NestedNameSpecifier qualifier) {
    std::vector<std::string> parts;
    // substrate generic algorithm: each qualifier only yields its own prefix,
    // so the sequence of parts does not exist until this walk produces it --
    // an unfold over a linked structure, not a fold over a range that is
    // already there to iterate.
    while (qualifier.getKind() == clang::NestedNameSpecifier::Kind::Namespace) {
        const clang::NamespaceAndPrefix np    = qualifier.getAsNamespaceAndPrefix();
        const auto*                     named = llvm::dyn_cast_or_null<clang::NamedDecl>(np.Namespace);
        if (named == nullptr)
            return std::nullopt;
        parts.push_back(named->getNameAsString());
        qualifier = np.Prefix;
    }
    // Only a fully-resolved chain counts; anything rooted in a type or a
    // dependent specifier is left alone.
    if (qualifier.getKind() != clang::NestedNameSpecifier::Kind::Null &&
        qualifier.getKind() != clang::NestedNameSpecifier::Kind::Global)
        return std::nullopt;
    if (parts.empty())
        return std::nullopt;
    std::reverse(parts.begin(), parts.end());
    return parts | std::views::join_with(std::string_view("::")) | std::ranges::to<std::string>();
}

bool imported_qualifier_is_droppable(const clang::NamedDecl*       decl,
                                     clang::NestedNameSpecifierLoc qualifier,
                                     const std::set<std::string>&  drop) {
    if (decl == nullptr || !qualifier)
        return false;
    const std::optional<std::string> written = qualifier_namespace_name(qualifier.getNestedNameSpecifier());
    const std::optional<std::string> owner   = declaration_namespace_name(decl);
    return written && owner && !drop.contains(*written) && drop.contains(*owner);
}

std::vector<ExposUse> expos_uses(clang::Decl*                                     root,
                                 const std::map<const clang::Decl*, std::string>& expos,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts) {
    return find_expos_uses(root, expos, sm, lang_opts);
}

std::vector<ExposUse> expos_uses(clang::Stmt*                                     root,
                                 const std::map<const clang::Decl*, std::string>& expos,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts) {
    return find_expos_uses(root, expos, sm, lang_opts);
}

std::vector<std::pair<unsigned, unsigned>>
namespace_qualifier_edits(clang::Stmt*                                     root,
                          const std::set<std::string>&                     drop,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::map<const clang::Decl*, std::string>& expos) {
    std::vector<std::pair<unsigned, unsigned>> out;
    if (root != nullptr && !drop.empty()) {
        QualifierDropper dropper(drop, sm, lang_opts, out);
        dropper.TraverseStmt(root);
        drop_edits_inside_expos_qualifiers(out, expos_uses(root, expos, sm, lang_opts));
    }
    return out;
}

std::vector<std::pair<unsigned, unsigned>>
namespace_qualifier_edits(clang::Decl*                                     root,
                          const std::set<std::string>&                     drop,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::map<const clang::Decl*, std::string>& expos) {
    std::vector<std::pair<unsigned, unsigned>> out;
    if (root != nullptr && !drop.empty()) {
        QualifierDropper dropper(drop, sm, lang_opts, out);
        dropper.TraverseDecl(root);
        drop_edits_inside_expos_qualifiers(out, expos_uses(root, expos, sm, lang_opts));
    }
    return out;
}

} // namespace beman::specgen::frontend
