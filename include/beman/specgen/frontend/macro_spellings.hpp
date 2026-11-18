// include/beman/specgen/frontend/macro_spellings.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A specifier spelled through a macro, written as what it expands to
// (issue #349).

#ifndef BEMAN_SPECGEN_FRONTEND_MACRO_SPELLINGS_HPP
#define BEMAN_SPECGEN_FRONTEND_MACRO_SPELLINGS_HPP

#include <beman/specgen/ir.hpp>

#include <clang/Lex/Preprocessor.h>

#include <map>
#include <string>

namespace beman::specgen::frontend {

// Each object-like macro of the translation unit whose expansion is nothing or
// keywords alone, such as `#define DEMO_CONSTEXPR constexpr`, mapped to that
// expansion, its tokens separated by single spaces. A macro whose expansion is
// another such macro is mapped to that one's. Anything else -- a function-like
// macro, or one whose expansion holds a number, a name that is not such a
// macro, or an attribute -- is not a specifier, and is left as the header
// writes it.
std::map<std::string, std::string> specifier_macros(const clang::Preprocessor& pp);

// `code` with each identifier `macros` maps written as its expansion. The text
// of a comment, a string or character literal, and a span is not code to
// expand. A macro that expands to nothing takes a run of spaces after it on its
// line with it, or, when none follow it, the run before it, so `inline
// DEMO_NOTHING int` reads `inline int`. The spans and line-break hints after
// each replacement move with the text.
ir::CodeText expand_specifier_macros(ir::CodeText code, const std::map<std::string, std::string>& macros);

// The same over every declaration the document extracted: each extracted
// synopsis, the declarations a synopsis is checked against, and each item's
// signatures. A synopsis whose code is authored (`\verbatim-synopsis`) or
// inherited (`\asdraft-synopsis`) keeps it, which is what its `declared` says;
// a code block and a body's *Equivalent to* are code, not declarations.
void expand_specifier_macros(ir::Document& document, const std::map<std::string, std::string>& macros);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_MACRO_SPELLINGS_HPP
