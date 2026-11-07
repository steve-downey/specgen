// src/beman/specgen/frontend/directive_sets.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/directive_sets.hpp>

#include <beman/specgen/ir.hpp>

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/markers.hpp>
#include <beman/specgen/frontend/decl_projections.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/record_members.hpp>

#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// A member's exposition-only name candidate: a data member, static or not, a
// type alias, or a method, including the underlying method of a
// FunctionTemplateDecl. A static data member and a static data member template
// used to be missing, so an `\expos` one never reached the expos set and its
// synopsis line was dropped as unmarked private data (issue #172).
// Hidden friends
// remain namespace functions rather than class exposition members.
const clang::NamedDecl* as_field_or_method(const clang::Decl* member) {
    if (const auto* fd = llvm::dyn_cast<clang::FieldDecl>(member))
        return fd;
    if (llvm::isa<clang::VarDecl, clang::VarTemplateDecl>(member))
        return llvm::cast<clang::NamedDecl>(member);
    if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(member))
        return alias;
    if (const auto* md = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(member_function_or_template(member)))
        return md;
    // A nested class (issue #80).  A view's iterator is one, private and named
    // by every `begin`, `end` and `operator++` signature the class publishes,
    // so it is exactly the shape `\expos` is for: rendered under an exposition
    // name rather than dropped as a private member the wording goes on naming.
    if (const auto* nested = llvm::dyn_cast<clang::CXXRecordDecl>(member);
        nested != nullptr && nested->getIdentifier() != nullptr)
        return nested;
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(member))
        return tmpl;
    return nullptr;
}

// The namespace path enclosing `decl`, most-enclosing first, joined with
// "::" ("" if `decl` is not lexically inside any named namespace). Same
// unfold-then-join shape as qualifier_namespace_name above (this file's
// second "::"-join call site; reuses its exact
// `std::views::join_with(std::string_view("::"))` expression rather than a
// third spelling -- a bare `"::"` literal would drag its NUL terminator into
// the join).
// The named namespaces enclosing `decl`, outermost first.
std::vector<const clang::NamespaceDecl*> namespace_chain(const clang::Decl* decl) {
    std::vector<const clang::NamespaceDecl*> chain;
    // substrate generic algorithm: DeclContext parent-chain walk -- an
    // unfold over a linked structure, not a fold over a range that already
    // exists, same shape as qualifier_namespace_name's NestedNameSpecifier
    // walk (step 07).
    for (const clang::DeclContext* ctx = decl->getDeclContext(); ctx != nullptr; ctx = ctx->getParent())
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(ctx); ns != nullptr && !ns->isAnonymousNamespace())
            chain.push_back(ns);
    std::reverse(chain.begin(), chain.end());
    return chain;
}

std::string join_namespace_names(std::span<const clang::NamespaceDecl* const> chain) {
    return chain | std::views::transform([](const clang::NamespaceDecl* ns) { return ns->getNameAsString(); }) |
           std::views::join_with(std::string_view("::")) | std::ranges::to<std::string>();
}

std::string namespace_path(const clang::Decl* decl) { return join_namespace_names(namespace_chain(decl)); }

// A namespace's own qualified path, by the same join as a declaration inside
// it, so the specified set and the chain walk above agree on inline
// namespaces, which getQualifiedNameAsString may or may not print.
std::string qualified_namespace_path(const clang::NamespaceDecl* ns) {
    std::vector<const clang::NamespaceDecl*> chain = namespace_chain(ns);
    chain.push_back(ns);
    return join_namespace_names(chain);
}

} // namespace

std::set<const clang::Decl*> build_omit_set(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto is_marked = [&sm](const clang::FunctionDecl* fn) {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(fn, sm);
        return suppresses_generated_entity(dirs);
    };

    // Out-of-line function definitions, keyed to their in-class declaration.
    std::set<const clang::Decl*> omit =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter(is_marked) |
        std::views::transform([](const clang::FunctionDecl* fn) { return fn->getFirstDecl()->getCanonicalDecl(); }) |
        std::ranges::to<std::set<const clang::Decl*>>();

    // In-class members of a class or class template, including member function
    // templates. The trailing `ranges::to` owns each record's members across
    // the `transform(...) | join` below.
    const auto omit_members_of = [&is_marked](const clang::CXXRecordDecl* record) {
        return record->decls() |
               std::views::transform([](const clang::Decl* member) { return member_function_or_template(member); }) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter(is_marked) |
               std::views::transform([](const clang::FunctionDecl* fn) { return fn->getCanonicalDecl(); }) |
               std::ranges::to<std::vector<const clang::Decl*>>();
    };
    omit.insert_range(decls | std::views::transform(as_record_decl) |
                      std::views::filter([](const clang::CXXRecordDecl* record) {
                          return record != nullptr && record->isThisDeclarationADefinition();
                      }) |
                      std::views::transform(omit_members_of) | std::views::join);

    return omit;
}

bool is_namespace_expos_candidate(const clang::Decl* decl) {
    if (llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
        llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::ClassTemplateDecl>(decl))
        return true;
    // An enumeration belongs here for the same reason it is a wording entity
    // at all (issue #68): its definition is its interface, so an
    // exposition-only one renders as a standalone synopsis under its exposid
    // name. Without this it took the *wording* path with an empty description,
    // which made it an `\also` follower and folded its declaration into
    // whatever item preceded it.
    if (const auto* enumeration = llvm::dyn_cast<clang::EnumDecl>(decl))
        return enumeration->getDeclContext()->isFileContext();
    // A specialization of one, which is the same entity as its primary and so
    // shares its disposition (issue #49). The variable-template kinds reach
    // the VarDecl test below on their own; a class-template specialization is
    // a CXXRecordDecl and would otherwise be routed as an ordinary class.
    if (llvm::isa<clang::ClassTemplateSpecializationDecl>(decl))
        return true;
    // A plain class, for the reason a class template is one (issue #142): the
    // draft's exposition-only helpers include non-template classes. A defined
    // one takes the ordinary class arms, like a defined class template; a
    // forward declaration of a marked class is reproduced as an exposition-only
    // declaration, since the synopsis needs it to compile (issue #151). The
    // templated record of a class template is that template's, and an
    // unnamed class has no name to rewrite.
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
        return record->getIdentifier() != nullptr && record->getDescribedClassTemplate() == nullptr &&
               record->getDeclContext()->isFileContext();
    if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(decl))
        return alias->getDeclContext()->isFileContext();
    const auto* variable = llvm::dyn_cast<clang::VarDecl>(decl);
    return variable != nullptr && variable->getDeclContext()->isFileContext();
}

bool is_defined_record(const clang::Decl* decl) {
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl)) {
        const clang::CXXRecordDecl* templated = tmpl->getTemplatedDecl();
        return templated != nullptr && templated->isThisDeclarationADefinition();
    }
    const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
    return record != nullptr && record->isThisDeclarationADefinition();
}

const clang::NamedDecl* as_namespace_entity(const clang::Decl* decl) {
    const bool entity_kind = llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
                             llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::TypeAliasDecl>(decl) ||
                             llvm::isa<clang::VarDecl>(decl) || llvm::isa<clang::EnumDecl>(decl);
    if (!entity_kind || !decl->getDeclContext()->isFileContext())
        return nullptr;
    return llvm::cast<clang::NamedDecl>(decl);
}

std::map<const clang::Decl*, std::string> build_expos_set(const std::vector<clang::Decl*>& decls,
                                                          const clang::SourceManager&      sm) {
    using ExposEntry = std::pair<const clang::Decl*, std::string>;

    const auto is_marked  = [&sm](const clang::NamedDecl* named) { return docblock_directives(named, sm).expos; };
    const auto expos_name = [&sm](const clang::NamedDecl* named) {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(named, sm);
        return dirs.expos_name.value_or(named->getIdentifier() != nullptr
                                            ? beman::specgen::lowering::exposid_name(named->getName().str())
                                            : std::string{});
    };
    // Anonymous records are transparent here: their source members are the
    // candidates, while Clang's implicit injected projections are not.
    // The marked members of a record, and of the records nested in it: a
    // nested class's own state is exposition for the same reason its class's
    // is, and an extracted body that names it has to say the exposition name
    // (issue #80).
    const auto expos_members_of = [&](this const auto&            self,
                                      const clang::CXXRecordDecl* record) -> std::vector<ExposEntry> {
        std::vector<ExposEntry> out;
        // substrate generic algorithm: a filter-map over one record's members
        // that also descends -- the recursion is why this is a loop and not a
        // pipeline built in place.
        for (const RealRecordMember& member : real_record_members(record)) {
            if (const clang::NamedDecl* named = as_field_or_method(member.decl); named != nullptr && is_marked(named))
                out.emplace_back(named->getCanonicalDecl(), expos_name(named));
            if (const clang::CXXRecordDecl* nested = as_record_decl(member.decl);
                nested != nullptr && nested->isThisDeclarationADefinition())
                out.append_range(self(nested));
        }
        return out;
    };

    std::map<const clang::Decl*, std::string> expos;
    expos.insert_range(decls | std::views::transform(as_record_decl) |
                       std::views::filter([](const clang::CXXRecordDecl* record) {
                           return record != nullptr && record->isThisDeclarationADefinition();
                       }) |
                       std::views::transform(expos_members_of) | std::views::join);
    expos.insert_range(
        decls | std::views::filter(is_namespace_expos_candidate) |
        std::views::transform([](const clang::Decl* decl) { return llvm::cast<clang::NamedDecl>(decl); }) |
        std::views::filter(is_marked) | std::views::transform([&expos_name](const clang::NamedDecl* named) {
            return std::pair<const clang::Decl*, std::string>{named->getCanonicalDecl(), expos_name(named)};
        }));

    // A namespace using-declaration introduces no declaration of its own for
    // a synopsis to print, but `\expos` on it still marks the declarations it
    // makes reachable under that name. Map every shadow's target so a use in
    // this header -- or in a sibling header that includes it -- takes the
    // ordinary exposition-use rewrite path (issue #113).
    const auto expos_targets_of_using = [&](const clang::UsingDecl* using_decl) {
        const std::string display = expos_name(using_decl);
        return using_decl->shadows() | std::views::transform([display](const clang::UsingShadowDecl* shadow) {
                   return std::array{
                       ExposEntry{shadow->getCanonicalDecl(), display},
                       ExposEntry{shadow->getTargetDecl()->getCanonicalDecl(), display},
                   };
               }) |
               std::views::join | std::ranges::to<std::vector<ExposEntry>>();
    };
    expos.insert_range(
        decls | std::views::transform([](const clang::Decl* decl) { return llvm::dyn_cast<clang::UsingDecl>(decl); }) |
        std::views::filter([](const clang::UsingDecl* using_decl) { return using_decl != nullptr; }) |
        std::views::filter(is_marked) | std::views::transform(expos_targets_of_using) | std::views::join);

    // A second pass, because a specialization takes its name from the primary
    // and the primary need not have been seen first. It is the same entity, so
    // it cannot be exposition-only under one name and not under another
    // (issue #49) -- and its own markers do not enter into it: `\expos` on a
    // specialization was accepted and ignored, `\also` made it a wording item,
    // and neither kept the raw implementation spelling out of the wording.
    expos.insert_range(decls | std::views::transform(specialized_primary) |
                       std::views::filter([&expos](const auto& pair) {
                           return pair.second != nullptr && expos.contains(pair.second->getCanonicalDecl());
                       }) |
                       std::views::transform([&expos](const auto& pair) {
                           return std::pair<const clang::Decl*, std::string>{
                               pair.first->getCanonicalDecl(), expos.at(pair.second->getCanonicalDecl())};
                       }));
    return expos;
}

std::set<std::string> build_namespace_drop_set(const std::vector<clang::Decl*>& decls) {
    std::set<std::string> paths = decls | std::views::transform(namespace_path) |
                                  std::views::filter([](const std::string& path) { return !path.empty(); }) |
                                  std::ranges::to<std::set<std::string>>();
    paths.insert("std");

    const auto nested_in_another = [&paths](const std::string& path) {
        return std::ranges::any_of(paths, [&path](const std::string& other) {
            return other.size() < path.size() && path.starts_with(other) && path[other.size()] == ':';
        });
    };
    return paths | std::views::filter(std::not_fn(nested_in_another)) | std::ranges::to<std::set<std::string>>();
}

SeeBelowMap build_seebelow_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto marked_target = [&sm](const clang::FunctionDecl* fn) {
        return seebelow_target(docblock_directives(fn, sm));
    };

    // Out-of-line function definitions, keyed to their in-class declaration.
    SeeBelowMap result =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter([&](const clang::FunctionDecl* fn) { return marked_target(fn).has_value(); }) |
        std::views::transform([&](const clang::FunctionDecl* fn) {
            return std::pair<const clang::Decl*, SeeBelowTarget>{fn->getFirstDecl()->getCanonicalDecl(),
                                                                 *marked_target(fn)};
        }) |
        std::ranges::to<SeeBelowMap>();

    // In-class members of a class or class template: member_as_function_or_friend's
    // FunctionDecl-or-FriendDecl unwrap, plus (unlike build_omit_set) a plain
    // in-class function template -- \seebelow can mark one, and unlike a
    // friend target this is not ambiguous with anything build_omit_set already
    // recognizes, so it is a local addition rather than a change to the shared
    // projection.
    const auto seebelow_members_of = [&marked_target](const clang::CXXRecordDecl* record) {
        const std::vector<const clang::Decl*> members(record->decls_begin(), record->decls_end());
        return members | std::views::transform([](const clang::Decl* member) -> const clang::FunctionDecl* {
                   if (const clang::FunctionDecl* fn = member_as_function_or_friend(member))
                       return fn;
                   if (const auto* ft = llvm::dyn_cast<clang::FunctionTemplateDecl>(member))
                       return ft->getTemplatedDecl();
                   return nullptr;
               }) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter([&](const clang::FunctionDecl* fn) { return marked_target(fn).has_value(); }) |
               std::views::transform([&](const clang::FunctionDecl* fn) {
                   return std::pair<const clang::Decl*, SeeBelowTarget>{fn->getCanonicalDecl(), *marked_target(fn)};
               }) |
               std::ranges::to<std::vector<std::pair<const clang::Decl*, SeeBelowTarget>>>();
    };
    result.insert_range(decls | std::views::transform(as_record_decl) |
                        std::views::filter([](const clang::CXXRecordDecl* record) {
                            return record != nullptr && record->isThisDeclarationADefinition();
                        }) |
                        std::views::transform(seebelow_members_of) | std::views::join);

    return result;
}

SpecifiedNamespaces build_specified_namespace_set(const std::vector<clang::Decl*>& decls,
                                                  clang::ASTContext&               ctx,
                                                  const clang::SourceManager&      sm) {
    namespace db = beman::specgen::document_build;
    SpecifiedNamespaces out;

    // Where a docblock's finding is reported: its first markup line, in the
    // file it is in, the same position docblock_diagnostics puts a line-0
    // grammar finding.
    const auto report = [&](const clang::RawComment* rc, std::size_t start, std::string message) {
        const clang::SourceLocation loc = rc->getBeginLoc().getLocWithOffset(static_cast<int>(start));
        out.diagnostics.push_back(db::Diagnostic{beman::specgen::Severity::Error,
                                                 sm.getSpellingLineNumber(loc),
                                                 std::move(message),
                                                 diagnostic_file_name(sm, sm.getFileID(loc))});
    };

    const auto read_namespace = [&](const clang::NamespaceDecl* ns) {
        const clang::RawComment* rc = attached_raw_comment(ns);
        if (rc == nullptr)
            return;
        const llvm::StringRef            raw   = rc->getRawText(sm);
        const std::optional<std::size_t> start = docblock_start(raw);
        if (!start)
            return;
        const grammar::ParseResult parsed  = grammar::parse_docblock(raw.substr(*start).str());
        const lowering::Lowered    lowered = lowering::lower(parsed.block);
        // A docblock without the marker is not the namespace's at all: a
        // detached `\\verbatim-itemdecl` block written just above a
        // `namespace` line is attached to it by Clang and is classify's
        // standalone node, reported there if anything is wrong with it.
        if (!lowered.directives.specified)
            return;
        out.marked.push_back(ns);
        out.paths.insert(qualified_namespace_path(ns));
        out.diagnostics.append_range(docblock_diagnostics(rc, *start, parsed.diags, sm));
        grammar::Markers rest = lowered.directives;
        rest.specified        = false;
        if (rest != grammar::Markers{} || beman::specgen::ir::has_description(lowered.descr) ||
            parsed.block.verbatim_itemdecl)
            report(rc, *start, "a namespace accepts only \\specified: it has no wording of its own");
    };

    // Every namespace in the translation unit, redeclarations included: the
    // marker may sit on any one of them, in any header this parse read.
    const auto visit = [&](this const auto& self, const clang::DeclContext* context) -> void {
        // substrate generic algorithm: recursive descent over the decl tree,
        // the same shape collect_top_level_decl walks; the recursion is the
        // algorithm and the loop only feeds it.
        for (const clang::Decl* child : context->decls()) {
            if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(child)) {
                if (!ns->isAnonymousNamespace() && !sm.isInSystemHeader(ns->getLocation()))
                    read_namespace(ns);
                self(ns);
            } else if (const auto* linkage = llvm::dyn_cast<clang::LinkageSpecDecl>(child)) {
                self(linkage);
            }
        }
    };
    visit(ctx.getTranslationUnitDecl());

    // The marker off a namespace: silently taking no effect is the failure
    // mode every misplaced marker here refuses.
    std::ranges::for_each(
        decls | std::views::filter([&](const clang::Decl* decl) { return docblock_directives(decl, sm).specified; }),
        [&](const clang::Decl* decl) {
            const clang::RawComment* rc = attached_raw_comment(decl);
            if (const std::optional<std::size_t> start = docblock_start(rc->getRawText(sm)))
                report(rc, *start, "\\specified applies only to a namespace");
        });
    // Two walks, one report: in file order, as a reader meets them.
    std::ranges::stable_sort(out.diagnostics, [](const db::Diagnostic& a, const db::Diagnostic& b) {
        return std::tie(a.file, a.line) < std::tie(b.file, b.line);
    });
    return out;
}

RenderedNamespace rendered_namespace(const clang::Decl*           decl,
                                     const std::set<std::string>& ns_drop_set,
                                     const std::set<std::string>& specified) {
    const std::vector<const clang::NamespaceDecl*> chain       = namespace_chain(decl);
    const auto                                     prefix_path = [&chain](std::size_t count) {
        return join_namespace_names(std::span(chain).first(count));
    };

    // The drop-set prefix is prefix-minimal, so at most one prefix matches;
    // scanning every length rather than stopping at the first keeps that an
    // invariant of build_namespace_drop_set and not of this function.
    std::size_t dropped = 0;
    // substrate generic algorithm: a scan over prefix lengths whose state is
    // the longest match so far.
    for (std::size_t count = 1; count <= chain.size(); ++count)
        if (ns_drop_set.contains(prefix_path(count)))
            dropped = count;

    const bool        std_rooted = !chain.empty() && chain.front()->getName() == "std";
    RenderedNamespace out;
    // substrate generic algorithm: a prefix scan that stops at the first
    // component the author did not specify; what came before it is the
    // answer, so this is a take_while whose predicate reads the accumulated
    // prefix.
    for (std::size_t index = dropped; index < chain.size(); ++index) {
        if (!std_rooted && !specified.contains(prefix_path(index + 1)))
            break;
        const clang::NamespaceDecl* ns = chain[index];
        if (!out.qualifier.empty()) {
            out.qualifier += "::";
            out.definition += "::";
        }
        out.qualifier += ns->getNameAsString();
        if (ns->isInline())
            out.definition += "inline ";
        out.definition += ns->getNameAsString();
    }
    return out;
}

std::string declarator_qualifier(const clang::NamedDecl*      decl,
                                 const std::set<std::string>& ns_drop_set,
                                 const std::set<std::string>& specified) {
    const clang::NamedDecl* declarator = decl;
    if (const auto* tmpl = llvm::dyn_cast<clang::TemplateDecl>(decl);
        tmpl != nullptr && tmpl->getTemplatedDecl() != nullptr)
        declarator = tmpl->getTemplatedDecl();
    const auto* declared = llvm::dyn_cast<clang::DeclaratorDecl>(declarator);
    if (declared == nullptr || !llvm::isa<clang::FunctionDecl, clang::VarDecl>(declared) ||
        llvm::isa<clang::CXXMethodDecl>(declared))
        return {};
    // A hidden friend is a namespace function declared in a class body, and
    // the draft writes its itemdecl bare under the class.
    if (!declared->getDeclContext()->isFileContext() || !declared->getLexicalDeclContext()->isFileContext())
        return {};
    if (static_cast<bool>(declared->getQualifierLoc()))
        return {};
    return rendered_namespace(declared, ns_drop_set, specified).qualifier;
}

FreestandingMap build_freestanding_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto marked_kind = [&sm](const clang::FunctionDecl* fn) -> std::optional<FreestandingKind> {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(fn, sm);
        if (dirs.freestanding_deleted)
            return FreestandingKind::Deleted;
        if (dirs.freestanding)
            return FreestandingKind::Freestanding;
        return std::nullopt;
    };

    FreestandingMap result =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter([&](const clang::FunctionDecl* fn) { return marked_kind(fn).has_value(); }) |
        std::views::transform([&](const clang::FunctionDecl* fn) {
            return std::pair<const clang::Decl*, FreestandingKind>{fn->getFirstDecl()->getCanonicalDecl(),
                                                                   *marked_kind(fn)};
        }) |
        std::ranges::to<FreestandingMap>();

    const auto marked_members_of = [&marked_kind](const clang::CXXRecordDecl* record) {
        const std::vector<const clang::Decl*> members(record->decls_begin(), record->decls_end());
        return members | std::views::transform(member_function_or_template) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter([&](const clang::FunctionDecl* fn) { return marked_kind(fn).has_value(); }) |
               std::views::transform([&](const clang::FunctionDecl* fn) {
                   return std::pair<const clang::Decl*, FreestandingKind>{fn->getCanonicalDecl(), *marked_kind(fn)};
               }) |
               std::ranges::to<std::vector<std::pair<const clang::Decl*, FreestandingKind>>>();
    };
    result.insert_range(decls | std::views::transform(as_record_decl) |
                        std::views::filter([](const clang::CXXRecordDecl* record) {
                            return record != nullptr && record->isThisDeclarationADefinition();
                        }) |
                        std::views::transform(marked_members_of) | std::views::join);
    return result;
}

} // namespace beman::specgen::frontend
