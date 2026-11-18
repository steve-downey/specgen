// include/beman/specgen/frontend/base_members.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The member functions a class has from its base classes, as declarations the
// synopsis check can read (synopsis_check.hpp, issue #342).

#ifndef BEMAN_SPECGEN_FRONTEND_BASE_MEMBERS_HPP
#define BEMAN_SPECGEN_FRONTEND_BASE_MEMBERS_HPP

#include <beman/specgen/ir.hpp>

#include <clang/AST/DeclCXX.h>
#include <clang/Basic/LangOptions.h>

#include <vector>

namespace beman::specgen::frontend {

// One entry per class a member of `record` may come from: each base class's
// definition, and, for a base that names a specialization of a template that
// depends on `record`'s own parameters, the definition of every specialization
// it could be -- the primary template, each partial specialization, each
// explicit specialization -- since which one applies is not known until
// `record` is instantiated. The bases of each of those follow, the same way.
//
// Each entry is the class's public and protected member functions and member
// function templates, as Clang prints their declarations (bodies left out,
// macros expanded), one per line and each ending in `;`. A constructor, a
// destructor and an `operator=` are left out: a derived class declares its
// own, whether it writes them or not.
std::vector<ir::CodeText> base_class_members(const clang::CXXRecordDecl* record, const clang::LangOptions& lang_opts);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_BASE_MEMBERS_HPP
