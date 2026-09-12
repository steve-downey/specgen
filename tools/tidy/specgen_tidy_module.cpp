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

#include <clang/ASTMatchers/ASTMatchers.h>

#include <cstddef>

namespace beman::specgen::tidy {

namespace {

// The exact marker docs/CODING_RULES.md ("No raw loops") prescribes, matched
// as literal text the way tools/check-raw-loops.cmake matches it — which
// keeps that gate's one recorded limit, a marker inside a string literal
// counting as a mark, rather than trading it for a different rule.
constexpr llvm::StringRef kMarker = "substrate generic algorithm";

// A line whose first non-blank characters open or continue a comment: `//`,
// `/*`, or a block comment's continuation `*`. The same three prefixes the
// text gate's upward walk accepts.
bool is_comment_line(llvm::StringRef line) {
    const llvm::StringRef trimmed = line.ltrim(" \t");
    return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*");
}

} // namespace

// The no-raw-loops check (docs/CODING_RULES.md "No raw loops"): every `for`,
// range-`for`, `while`, and `do` outside a substrate generic algorithm is a
// defect unless it carries the marker on the loop line (trailing-comment
// form) or in the contiguous run of comment lines directly above it (block
// form). The AST supplies the loop sites — one per template, at the macro
// *use* for a loop a macro produces — and the raw source buffer supplies the
// marker rule, read exactly as tools/check-raw-loops.cmake reads it, so the
// doctrine and the marked sites do not change with the mechanism.
class NoRawLoopsCheck : public clang::tidy::ClangTidyCheck {
  public:
    using clang::tidy::ClangTidyCheck::ClangTidyCheck;

    void registerMatchers(clang::ast_matchers::MatchFinder* finder) override {
        using namespace clang::ast_matchers;
        // One site per loop as written: a loop in a template body is
        // matched in the template, not once per instantiation.
        finder->addMatcher(forStmt(unless(isInTemplateInstantiation())).bind("loop"), this);
        finder->addMatcher(cxxForRangeStmt(unless(isInTemplateInstantiation())).bind("loop"), this);
        finder->addMatcher(whileStmt(unless(isInTemplateInstantiation())).bind("loop"), this);
        finder->addMatcher(doStmt(unless(isInTemplateInstantiation())).bind("loop"), this);
    }

    void check(const clang::ast_matchers::MatchFinder::MatchResult& result) override {
        const auto* loop = result.Nodes.getNodeAs<clang::Stmt>("loop");
        if (loop == nullptr)
            return;
        const clang::SourceManager& sm = *result.SourceManager;
        // The expansion location: a loop produced by a macro is a site at
        // the macro's use, which the text gate could not see at all.
        const clang::SourceLocation site = sm.getExpansionLoc(loop->getBeginLoc());
        if (site.isInvalid())
            return;
        const auto [file, offset]    = sm.getDecomposedLoc(site);
        bool                  broken = false;
        const llvm::StringRef buffer = sm.getBufferData(file, &broken);
        if (broken)
            return;

        // The loop's own line first: the trailing-comment form.
        // (llvm::StringRef::rfind searches [0, From), so npos + 1 lands on
        // offset 0 for a site on the buffer's first line.)
        const std::size_t line_begin = buffer.rfind('\n', offset) + 1;
        std::size_t       line_end   = buffer.find('\n', offset);
        if (line_end == llvm::StringRef::npos)
            line_end = buffer.size();
        if (buffer.slice(line_begin, line_end).contains(kMarker))
            return;

        // Then the contiguous comment block directly above: walk upward
        // while each line is a comment line, stopping at the first that is
        // not — a marker separated from its loop by code marks nothing.
        // substrate generic algorithm: an upward unfold over the buffer's
        // line structure with two early exits; there is no range of lines to
        // fold until this loop manufactures it.
        for (std::size_t begin = line_begin; begin > 0;) {
            const std::size_t     prev_begin = buffer.rfind('\n', begin - 1) + 1;
            const llvm::StringRef prev       = buffer.slice(prev_begin, begin - 1);
            if (!is_comment_line(prev))
                break;
            if (prev.contains(kMarker))
                return;
            begin = prev_begin;
        }

        diag(site,
             "raw loop outside a substrate generic algorithm; convert it to a named algorithm, or mark it "
             "'// substrate generic algorithm: <reason>' on the loop line or in the comment block directly above it");
    }
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
