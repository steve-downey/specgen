// src/beman/specgen/frontend/smoke_check.cpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/smoke_check.hpp>

#include <clang/Format/Format.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Core/Replacement.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/Error.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

SmokeResult smoke_check(std::string_view source) {
    SmokeResult result;

    // Tooling surface (design §3.1): parse into an AST with the flags the real
    // front end will use. -fsyntax-only keeps it to a parse; -fparse-all-comments
    // is what the raw-comment collection needs, exercised here so the flag is
    // proven wired.
    const std::vector<std::string>  args = {"-std=c++2c", "-fsyntax-only", "-fparse-all-comments"};
    std::unique_ptr<clang::ASTUnit> ast  = clang::tooling::buildASTFromCodeWithArgs(std::string(source), args);
    result.ast_built                     = ast != nullptr;

    // Formatting surface (design §3.6): run clang-format over a fixed snippet.
    // FormatStyle tuning to draft conventions is draft_format_style() in
    // frontend.cpp; here it only proves the surface links and produces output.
    const std::string                        code   = "int  f(  int x ){return  x ;}";
    const clang::format::FormatStyle         style  = clang::format::getLLVMStyle();
    const std::vector<clang::tooling::Range> ranges = {clang::tooling::Range(0, static_cast<unsigned>(code.size()))};
    const clang::tooling::Replacements       replacements = clang::format::reformat(style, code, ranges);
    if (llvm::Expected<std::string> formatted = clang::tooling::applyAllReplacements(code, replacements))
        result.formatted = *formatted;
    else
        llvm::consumeError(formatted.takeError());

    return result;
}

} // namespace beman::specgen::frontend
