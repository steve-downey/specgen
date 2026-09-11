// tools/tidy/specgen_tidy_module.cpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The specgen clang-tidy module (docs/plans/no-raw-loops-tidy-plugin.md),
// loaded into the *pinned* LLVM's own clang-tidy via --load (decision
// llvm-toolchain-pin: the pin covers clang-tidy and any plugin built against
// it). The static registrar below runs at dlopen time and appends this
// module to the same llvm::Registry the built-in modules use — which is why
// this library must never link against libclangTidy.a: that would hand it a
// private copy of the registry, and the checks would register where nobody
// reads. Its only link inputs are its own objects; every undefined symbol
// resolves at load time against the clang-tidy binary that dlopens it.
//
// ClangTidyModuleRegistry is declared in ClangTidyModule.h; the separate
// ClangTidyModuleRegistry.h spelling is deprecated in LLVM 22 and removed in
// 24, so only ClangTidyModule.h and ClangTidyCheck.h are included.

#include "clang-tidy/ClangTidyCheck.h"
#include "clang-tidy/ClangTidyModule.h"

namespace beman::specgen::tidy {

// The no-raw-loops check (docs/CODING_RULES.md "No raw loops"). This is the
// registration skeleton the plan's tidy-plugin-target stage proves loadable
// with --list-checks; the no-raw-loops-check stage supplies the loop
// matchers and the marker scan, keeping the doctrine exactly as
// tools/check-raw-loops.cmake reads it today.
class NoRawLoopsCheck : public clang::tidy::ClangTidyCheck {
  public:
    using clang::tidy::ClangTidyCheck::ClangTidyCheck;
};

class SpecgenModule : public clang::tidy::ClangTidyModule {
  public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories& factories) override {
        factories.registerCheck<NoRawLoopsCheck>("specgen-no-raw-loops");
    }
};

// The registrar object whose constructor performs the registration at
// dlopen time.
static clang::tidy::ClangTidyModuleRegistry::Add<SpecgenModule>
    specgen_module_registration("specgen-module", "beman.specgen house checks");

} // namespace beman::specgen::tidy
