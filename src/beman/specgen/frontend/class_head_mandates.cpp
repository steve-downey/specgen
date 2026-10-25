// src/beman/specgen/frontend/class_head_mandates.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/class_head_mandates.hpp>

#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>
#include <beman/specgen/frontend/constraints.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/requires_clauses.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
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

namespace {

bool is_template_parameter_pack(const clang::NamedDecl* param) {
    if (const auto* type = llvm::dyn_cast<clang::TemplateTypeParmDecl>(param))
        return type->isParameterPack();
    if (const auto* value = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(param))
        return value->isParameterPack();
    if (const auto* tmpl = llvm::dyn_cast<clang::TemplateTemplateParmDecl>(param))
        return tmpl->isParameterPack();
    return false;
}

// Spell the injected class-id from the declaration names Clang resolved. The
// condition text still comes from source tokens; this small synthesized name
// is wording context ("instantiates `optional<T>`"), not synopsis code.
std::string class_instantiation_name(const clang::CXXRecordDecl* record) {
    const clang::ClassTemplateDecl* tmpl = record->getDescribedClassTemplate();
    if (tmpl == nullptr)
        return record->getNameAsString();

    std::vector<std::string> arguments;
    // substrate generic algorithm: this is a transform with an early return
    // from the enclosing function when any parameter cannot be named; a view
    // cannot express that fallback without materializing an error channel.
    for (const clang::NamedDecl* param : *tmpl->getTemplateParameters()) {
        std::string name = param->getNameAsString();
        // An unnamed parameter has no source-level identifier with which to
        // spell an injected class-id. Keep the class name rather than invent
        // one; named parameters are the Beman-style input convention.
        if (name.empty())
            return record->getNameAsString();
        if (is_template_parameter_pack(param))
            name += "...";
        arguments.push_back(std::move(name));
    }
    if (arguments.empty())
        return record->getNameAsString();

    return std::format("{}<{}>",
                       record->getNameAsString(),
                       arguments | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>());
}

// The class-scope static_assert derivation (design §5.2): one adjacent
// general-subclause paragraph, plus the conjuncts it was folded from. The
// conjuncts are the same validator-only drift evidence a member's derived
// Mandates carries -- an authored class `\mandates` replaces the paragraph
// and inherits them, so drift detection still sees the assertions the
// authored text is standing in for.
struct ClassMandates {
    beman::specgen::ir::FreeParagraph          text;
    std::vector<beman::specgen::ir::Paragraph> conjuncts;
};

std::optional<ClassMandates> derive_class_mandates(const clang::CXXRecordDecl*                      record,
                                                   const clang::SourceManager&                      sm,
                                                   const clang::LangOptions&                        lang_opts,
                                                   const std::set<std::string>&                     ns_drop_set,
                                                   const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    std::vector<const clang::Expr*> leaves;
    // All direct assertions belong to this class's general wording, wherever
    // an access label happens to occur. Nested records are separate synopses
    // and are deliberately not recursed into.
    // substrate generic algorithm: a filtered flat-map into the shared leaves
    // accumulator; split_conjuncts recursively appends zero or more results.
    for (const clang::Decl* member : record->decls()) {
        const auto* assert_decl = llvm::dyn_cast<clang::StaticAssertDecl>(member);
        if (assert_decl != nullptr)
            split_conjuncts(assert_decl->getAssertExpr(), leaves);
    }
    if (leaves.empty())
        return std::nullopt;

    std::vector<ir::Paragraph> conditions = leaves | std::views::transform([&](const clang::Expr* leaf) {
                                                return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
                                            }) |
                                            std::ranges::to<std::vector<ir::Paragraph>>();

    ir::Paragraph text;
    text.push_back(ir::TextInline{"A program that instantiates "});
    text.push_back(ir::CodeInline{ir::CodeText{class_instantiation_name(record), {}}});
    text.push_back(ir::TextInline{" is ill-formed unless "});
    text.append_range(beman::specgen::conjuncts::join_sentence(conditions));
    return ClassMandates{ir::FreeParagraph{std::move(text)}, std::move(conditions)};
}

} // namespace

void attach_class_description(beman::specgen::document_build::SynopsisDecl&    out,
                              RecordDocblock&&                                 block,
                              const clang::CXXRecordDecl*                      record,
                              const clang::SourceManager&                      sm,
                              const clang::LangOptions&                        lang_opts,
                              const std::set<std::string>&                     ns_drop_set,
                              const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    out.descr = std::move(block.descr);
    // Within the class's own wording, uses of its exposition-only members
    // render as \exposid -- the same rewrite a member's description gets.
    rewrite_prose_expos(out.descr, expos_spellings(record, expos_set));

    std::optional<ClassMandates> derived = derive_class_mandates(record, sm, lang_opts, ns_drop_set, expos_set);
    if (!derived)
        return;
    const auto authored = std::ranges::find_if(out.descr.elements, [](const ir::DescriptionElement& element) {
        return element.kind == ir::ElementKind::Mandates && !element.derived;
    });
    if (authored != out.descr.elements.end())
        authored->conjuncts = std::move(derived->conjuncts);
    else
        out.general = std::move(derived->text);
}

void route_class_description(beman::specgen::document_build::SynopsisDecl&   out,
                             const clang::CXXRecordDecl*                     record,
                             const beman::specgen::lowering::ItemDirectives& directives) {
    namespace db = beman::specgen::document_build;

    if (!directives.at_anchor || directives.at_anchor->empty())
        return;
    if (!out.general && out.descr.elements.empty())
        return;

    const std::string name = record->getNameAsString();
    out.synopsis.roster.push_back(
        ir::SynopsisEntry{name, ir::Disposition::Routed, *directives.at_anchor, ir::MemberKind::Function, name});

    if (out.general) {
        out.pending.push_back(db::PendingItem{*directives.at_anchor, out.offset, std::move(*out.general)});
        out.general.reset();
    }
    if (!out.descr.elements.empty()) {
        out.pending.push_back(
            db::PendingItem{*directives.at_anchor, out.offset, ir::SpecItem{{}, std::move(out.descr)}});
        out.descr = {};
    }
}

} // namespace beman::specgen::frontend
