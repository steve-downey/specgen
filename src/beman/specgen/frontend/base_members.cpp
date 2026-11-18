// src/beman/specgen/frontend/base_members.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/base_members.hpp>

#include <beman/specgen/ir.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/TemplateName.h>
#include <clang/AST/Type.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/OperatorKinds.h>
#include <clang/Basic/Specifiers.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

const clang::CXXRecordDecl* defined(const clang::CXXRecordDecl* record) {
    return record != nullptr && record->hasDefinition() ? record->getDefinition() : nullptr;
}

// The classes a base specifier may stand for, defined ones only.
std::vector<const clang::CXXRecordDecl*> base_candidates(clang::QualType type) {
    std::vector<const clang::CXXRecordDecl*> out;
    const auto                               keep = [&](const clang::CXXRecordDecl* record) {
        if (const clang::CXXRecordDecl* definition = defined(record))
            out.push_back(definition);
    };
    if (const auto* specialization = type->getAs<clang::TemplateSpecializationType>();
        specialization != nullptr && specialization->isDependentType()) {
        const auto* tmpl =
            llvm::dyn_cast_or_null<clang::ClassTemplateDecl>(specialization->getTemplateName().getAsTemplateDecl());
        if (tmpl == nullptr)
            return out;
        keep(tmpl->getTemplatedDecl());
        llvm::SmallVector<clang::ClassTemplatePartialSpecializationDecl*> partials;
        tmpl->getPartialSpecializations(partials);
        std::ranges::for_each(partials, keep);
        std::ranges::for_each(tmpl->specializations(),
                              [&](const clang::ClassTemplateSpecializationDecl* explicit_spec) {
                                  if (explicit_spec->isExplicitSpecialization())
                                      keep(explicit_spec);
                              });
        return out;
    }
    keep(type->getAsCXXRecordDecl());
    return out;
}

// Whether `decl` is a member function a derived class has from its base.
const clang::Decl* inherited_function(const clang::Decl* decl) {
    const auto* tmpl   = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl);
    const auto* method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(
        tmpl != nullptr ? tmpl->getTemplatedDecl() : llvm::dyn_cast<clang::FunctionDecl>(decl));
    if (method == nullptr || method->isImplicit() || decl->getAccess() == clang::AS_private ||
        llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(method) ||
        method->getOverloadedOperator() == clang::OO_Equal)
        return nullptr;
    return decl;
}

ir::CodeText members(const clang::CXXRecordDecl* record, const clang::PrintingPolicy& policy) {
    std::string out;
    // substrate generic algorithm: prints each kept member into one buffer.
    for (const clang::Decl* decl : record->decls()) {
        if (const clang::Decl* kept = inherited_function(decl)) {
            llvm::raw_string_ostream os(out);
            kept->print(os, policy);
            os << ";\n";
        }
    }
    return ir::CodeText{std::move(out), {}};
}

void collect(const clang::CXXRecordDecl*            record,
             const clang::PrintingPolicy&           policy,
             std::set<const clang::CXXRecordDecl*>& seen,
             std::vector<ir::CodeText>&             out) {
    // substrate generic algorithm: a walk of the base graph that writes the
    // seen set and the result, each class once.
    for (const clang::CXXBaseSpecifier& base : record->bases()) {
        // substrate generic algorithm: the same walk, over what one base may be.
        for (const clang::CXXRecordDecl* candidate : base_candidates(base.getType())) {
            if (!seen.insert(candidate).second)
                continue;
            out.push_back(members(candidate, policy));
            collect(candidate, policy, seen, out);
        }
    }
}

} // namespace

std::vector<ir::CodeText> base_class_members(const clang::CXXRecordDecl* record, const clang::LangOptions& lang_opts) {
    clang::PrintingPolicy policy(lang_opts);
    policy.TerseOutput = true;
    std::set<const clang::CXXRecordDecl*> seen{record};
    std::vector<ir::CodeText>             out;
    collect(record, policy, seen, out);
    return out;
}

} // namespace beman::specgen::frontend
