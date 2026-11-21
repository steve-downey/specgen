// include/beman/specgen/backend/draft_edit.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — a paper's changes as an edit of the working draft's own LaTeX
// source (issue #343, docs/plans/draft-import.md `draft-source-edit`).
//
// The draft's sections are imported with where each listing came from
// (`ir::CodeText::origin`); the paper's document is the one the headers build,
// with `\asdraft` resolved against the same draft. The two are paired the way
// the differ pairs them, and each change is placed in the source text as an
// edit of only the bytes that change, so the draft's line breaks, index
// entries, comments and layout stay as they are.
//
// A change to the code of a declaration or a synopsis is placed token by
// token: a token added, removed or replaced, such as `constexpr`. Code is
// compared with its layout ignored, so a declaration the generator lays out
// differently from the draft is not a change. The two are first aligned
// declaration by declaration: one the paper adds goes on lines of its own, in
// the paper's layout, one it drops takes its lines, and only a declaration
// both have is edited token by token (issue #397).
//
// A change to an item's description replaces the numbered paragraphs that
// changed, each laid out as the draft's are, a sentence to a line, and leaves
// the rest as the draft has them. An item, a synopsis or a paragraph the paper
// adds is written after the draft's node the paper puts before it, as the LaTeX
// backend writes it; one the paper removes is taken out with the blank line
// after it (issue #346).
//
// Anything else -- a change inside a paragraph that is no item's description,
// a declaration joining or leaving an item, a subclause added or removed -- is
// an Error naming it, and is not placed. Clang-free.
#ifndef BEMAN_SPECGEN_BACKEND_DRAFT_EDIT_HPP
#define BEMAN_SPECGEN_BACKEND_DRAFT_EDIT_HPP

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/ir.hpp>

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::backend::draft_edit {

struct Finding {
    Severity    severity = Severity::Error;
    std::string where; // the subclause, and the declaration when there is one
    std::string message;
};

// Replace the bytes [begin, end) of a file with `text`.
struct Edit {
    std::size_t begin = 0;
    std::size_t end   = 0;
    std::string text;

    friend bool operator==(const Edit&, const Edit&) = default;
};

struct FileEdits {
    std::string       file; // as the import recorded it
    std::vector<Edit> edits;
};

struct Plan {
    std::vector<FileEdits> files; // only the files with an edit, each edit once, in file order
    std::vector<Finding>   findings;
};

// The draft's source text of a file the import recorded, for the edits that
// have to see it; nothing when it cannot be read.
using Reader = std::function<std::optional<std::string>(const std::string& file)>;

// The edits that turn the draft's sections, as imported, into the paper's.
// `draft` is the import of the sections `paper` generates. Without `read`, a
// changed description is replaced whole, and a removed node leaves its blank
// line.

Plan plan(const ir::Document& draft, const ir::Document& paper, Reader read = {});

// The plans of several documents of one paper, edits to one file together.
Plan merge(std::vector<Plan> plans);

// `text` with the edits made. They must not overlap; one that does, or that
// reaches past the end, is the error.
std::expected<std::string, std::string> apply(std::string_view text, std::span<const Edit> edits);

} // namespace beman::specgen::backend::draft_edit

#endif // BEMAN_SPECGEN_BACKEND_DRAFT_EDIT_HPP
