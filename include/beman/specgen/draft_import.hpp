// include/beman/specgen/draft_import.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the working draft's LaTeX, read back into the IR
// (docs/plans/draft-import.md, part 2).
//
// The importer is the inverse of the LaTeX backend over the part of the draft
// the backend writes: `\rSecN`, `\pnum`, `itemdecl`/`itemdescr`, `codeblock`,
// `itemize`, `note`/`example`, the element tags, and the inline macros the IR
// has a kind for. It never passes LaTeX through, except as a `RawInline` it
// reports a Warning for, so the three backends render what it reads the way
// they render everything else. Clang-free, like `diff`.
#ifndef BEMAN_SPECGEN_DRAFT_IMPORT_HPP
#define BEMAN_SPECGEN_DRAFT_IMPORT_HPP

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/ir.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::draft_import {

struct Diagnostic {
    Severity    severity = Severity::Warning;
    std::string section; // the stable name of the subclause the finding is in; empty for none
    std::string message;
};

struct Options {
    // Stable names of the subclauses to import, each with its subsections.
    // The result has one top-level section per name, in this order.
    std::vector<std::string> sections;
};

struct Result {
    ir::Document            document;
    std::vector<Diagnostic> diagnostics;
};

// Imports `sections` from one draft source file's text. A name the file does
// not contain is an Error and contributes no node. A construct the IR has no
// kind for is a Warning naming it and the subclause, and is kept as raw LaTeX.
Result import_sections(std::string_view tex, const Options& options);

} // namespace beman::specgen::draft_import

#endif // BEMAN_SPECGEN_DRAFT_IMPORT_HPP
