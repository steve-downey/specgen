// src/beman/specgen/frontend/body_uses.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/body_uses.hpp>

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/record_members.hpp>

#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
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

#include <algorithm>
#include <functional>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// Design §9 gives its leakage checker two severities, and the note one is
// about code the tool never prints: an undocumented helper that "appears only
// in non-extracted bodies". Every other validation rule reads something the IR
// already carries or something the front end can add beside it; this one
// cannot, and not by omission — a body with no `\effects-equiv` /
// `\returns-expr` marker is *by construction* the code that never becomes
// wording, so no node holds it and no amount of Tier-A reading finds it. What
// the front end records here is exactly that absence: the class members such a
// body names, for a validator that then asks the roster whether the reader
// could have seen them.
//
// The members a function body refers to, by name. Resolved references only —
// a `MemberExpr` or a `DeclRefExpr` whose declaration's parent is a class
// declared in the main file — which is narrower than scanning the body's text
// two ways that both matter. It excludes a local, a parameter and a `std::`
// name, none of which a roster ever holds; and it excludes a *dependent*
// member access (`this->helper()` through a dependent base), whose name
// resolves to no declaration here. The first exclusion is what keeps the
// Tier-A rule from reporting a body-local `tmp` that happens to share a name
// with a hidden member; the second is a miss, and is the direction every
// leakage rule errs in (a miss costs a finding, never invents one).
class BodyMemberCollector : public clang::RecursiveASTVisitor<BodyMemberCollector> {
  public:
    // `in_document` says which class a member must belong to for its name to
    // count: a class of the header's parse is in its main file, while a
    // source file's parse holds the classes in the files it includes (issue
    // #176).
    BodyMemberCollector(std::function<bool(const clang::CXXRecordDecl*)> in_document, std::set<std::string>& out)
        : in_document_(std::move(in_document)), out_(out) {}

    bool VisitMemberExpr(clang::MemberExpr* expr) {
        record(expr->getMemberDecl());
        return true;
    }

    // A static member function or static data member, and an unqualified call
    // to a member of the current instantiation that Sema resolved without an
    // implicit object argument, arrive as a plain reference rather than a
    // member access.
    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        record(expr->getDecl());
        return true;
    }

  private:
    void record(const clang::NamedDecl* decl) {
        if (decl == nullptr)
            return;
        const auto* parent = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(decl->getDeclContext());
        if (parent == nullptr || !in_document_(parent))
            return;
        const std::string name = decl->getNameAsString();
        if (!name.empty())
            out_.insert(name);
    }

    std::function<bool(const clang::CXXRecordDecl*)> in_document_;
    std::set<std::string>&                           out_;
};

// How a finding names the body it is about: `<class>::<name>` for a member,
// the bare name for a free function. Not a qualified name — the enclosing
// namespace maps onto `std` in the draft (design §3.5), so printing it would
// put a namespace in a diagnostic that the rendered wording deliberately does
// not have. This is the same "which class is this about" answer
// `ir::Synopsis::name` gives, spelled for a member instead of a type.
std::string body_owner_label(const clang::FunctionDecl* fn) {
    const clang::CXXRecordDecl* record = enclosing_record(fn);
    if (record == nullptr)
        return fn->getNameAsString();
    // A constructor's own name is the *injected class name*, which for a
    // class template carries the parameter list -- `optional<T>` -- and would
    // print as `optional::optional<T>`. The class already stands to the left
    // of the `::`, so the plain record name is both shorter and what a reader
    // writing the header saw.
    const std::string name =
        llvm::isa<clang::CXXConstructorDecl>(fn) ? record->getNameAsString() : fn->getNameAsString();
    return record->getNameAsString() + "::" + name;
}

} // namespace

bool has_unextracted_body(const clang::FunctionDecl* fn, const clang::SourceManager& sm) {
    if (fn == nullptr || !fn->isThisDeclarationADefinition() || fn->getBody() == nullptr)
        return false;
    const clang::RawComment* rc = attached_raw_comment(fn);
    if (rc == nullptr)
        return false;
    const llvm::StringRef raw = rc->getRawText(sm);
    if (!is_docblock_comment(raw))
        return false;

    const beman::specgen::lowering::Lowered lowered =
        beman::specgen::lowering::lower(beman::specgen::grammar::parse_docblock(raw.str()).block);
    if (suppresses_generated_entity(lowered.directives))
        return false;
    return std::ranges::none_of(lowered.descr.elements, [](const beman::specgen::ir::DescriptionElement& element) {
        return element.equivalent.has_value();
    });
}

std::vector<std::pair<std::string, std::string>>
body_member_uses(const clang::FunctionDecl* fn, std::function<bool(const clang::CXXRecordDecl*)> in_document) {
    std::set<std::string> names;
    BodyMemberCollector   collector(std::move(in_document), names);
    collector.TraverseStmt(fn->getBody());
    const std::string owner = body_owner_label(fn);
    return names | std::views::transform([owner](const std::string& member) { return std::pair{owner, member}; }) |
           std::ranges::to<std::vector>();
}

std::vector<beman::specgen::ir::BodyUse> collect_unextracted_uses(const std::vector<clang::Decl*>& decls,
                                                                  const clang::SourceManager&      sm) {
    const auto uses_of = [&sm](const clang::FunctionDecl* fn) {
        return body_member_uses(
            fn, [&sm](const clang::CXXRecordDecl* parent) { return sm.isInMainFile(parent->getLocation()); });
    };

    const auto bodies = [&sm](const clang::FunctionDecl* fn) { return has_unextracted_body(fn, sm); };

    // Out-of-line function definitions.
    std::vector<const clang::FunctionDecl*> functions = decls | std::views::transform(as_out_of_line_function) |
                                                        std::views::filter(bodies) |
                                                        std::ranges::to<std::vector<const clang::FunctionDecl*>>();

    // In-class definitions and hidden friends, the shape collect_inclass_items
    // attaches wording from.
    functions.append_range(decls | std::views::transform(as_record_decl) |
                           std::views::filter([](const clang::CXXRecordDecl* record) {
                               return record != nullptr && record->isThisDeclarationADefinition();
                           }) |
                           std::views::transform([](const clang::CXXRecordDecl* record) {
                               return record->decls() | std::views::transform(member_function_or_template) |
                                      std::ranges::to<std::vector<const clang::FunctionDecl*>>();
                           }) |
                           std::views::join | std::views::filter(bodies));

    // The set deduplicates and orders, so the IR channel does not depend on
    // which decl reached a member first — two constructors sharing an owner
    // label and a helper contribute one entry, which is also the finding a
    // reader wants (this class's wording leans on that helper).
    const std::set<std::pair<std::string, std::string>> found =
        functions | std::views::transform(uses_of) | std::views::join |
        std::ranges::to<std::set<std::pair<std::string, std::string>>>();

    return found | std::views::transform([](const auto& pair) {
               return beman::specgen::ir::BodyUse{pair.first, pair.second};
           }) |
           std::ranges::to<std::vector>();
}

} // namespace beman::specgen::frontend
