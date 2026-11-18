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

#include <filesystem>
#include <span>
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
    // The file the text is, recorded as where each listing was read from
    // (`ir::CodeText::origin`). The listings record their source whether or
    // not it is given.
    std::string source_name = {};
};

struct Result {
    ir::Document            document;
    std::vector<Diagnostic> diagnostics;
};

// Imports `sections` from one draft source file's text. A name the file does
// not contain is an Error and contributes no node. A construct the IR has no
// kind for is a Warning naming it and the subclause, and is kept as raw LaTeX.
Result import_sections(std::string_view tex, const Options& options);

// Author-written listing text -- a `\verbatim-synopsis` or `\verbatim-itemdecl`
// payload -- read as the draft's own listings are: an `@...@` escape made only
// of macros the span vocabulary has (`@\cv{}@`, `@\placeholder{ref}@`,
// `@\exposid{x}@`, `@\seebelow@`) becomes spans, and any other escape stays as
// written. So authored text and imported text of the same wording are the same
// IR, which is what lets the differ compare them.
ir::CodeText parse_listing(std::string_view raw);

// The class a synopsis declares, by the rule the importer names an imported one
// by: the one class or struct its code declares, or empty for none or several.
// An authored synopsis is named the same way when it is compared.
std::string declared_class_name(std::string_view code);

// The draft's source directory read for a document's sections (`--baseline-draft`).
struct TreeResult {
    // One top-level section per name found, in the order the names were given.
    ir::Document document;
    // Names no `.tex` file of the directory declares: new wording, not an error.
    std::vector<std::string> missing;
    // The files the sections were read from, for the dependency fragment.
    std::vector<std::string> sources;
    std::vector<Diagnostic>  diagnostics;
};

// Finds each of `names` (stable names) among the `\rSecN[name]` headings of the
// `*.tex` files directly in `directory`, and imports it as `import_sections`
// does. The first file, in name order, that declares a name supplies it. An
// unreadable directory is an Error and contributes nothing.
TreeResult import_from_directory(const std::filesystem::path& directory, std::span<const std::string> names);

} // namespace beman::specgen::draft_import

#endif // BEMAN_SPECGEN_DRAFT_IMPORT_HPP
