// include/beman/specgen/split_inline/rewrite.hpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// split_inline -- the rewrite that moves a class's in-class member function
// definitions out of line (docs/plans/split-inline.md). Given a parsed
// header, plan_rewrite decides which definitions move, what each one's
// out-of-line text is, where in the file it goes, and which members it
// refuses to touch; the answer is a list of byte-range edits over the main
// file's text, which apply_edits turns into the converted header. The two
// halves are separate so the edit list can be inspected and the application
// tested without Clang.

#ifndef BEMAN_SPECGEN_SPLIT_INLINE_REWRITE_HPP
#define BEMAN_SPECGEN_SPLIT_INLINE_REWRITE_HPP

#include <beman/specgen/foundation/enumeration.hpp>

#include <clang/Frontend/ASTUnit.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::split_inline {

// What happens to the comment block above a moved definition. `Keep` leaves
// every comment where it is: the tool is then a plain C++ restructuring.
// `Move` is the form for a header that already carries specgen work: the
// `//!` lines of the block travel with the definition, and the definition is
// placed under the `\rSec` section its `\ref` group names (plan, "Comments,
// and --move-comments").
enum class Comments { Keep, Move };

} // namespace beman::specgen::split_inline

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::split_inline::Comments> = std::array{
    beman::specgen::split_inline::Comments::Keep,
    beman::specgen::split_inline::Comments::Move,
};

namespace beman::specgen::split_inline {

struct Options {
    Comments comments = Comments::Keep;
    // The `\rSec` depth of a section header the tool writes for a `\ref`
    // group that has none, when no sibling section is there to copy the
    // depth from.
    int section_depth = 3;
};

// Replace the bytes [begin, end) of the main file with `text`. An insertion
// has begin == end. Edits never overlap; several insertions at one offset
// apply in list order.
struct Edit {
    unsigned    begin = 0;
    unsigned    end   = 0;
    std::string text;
};

// A member the tool left in the class, and why, at the line of its
// declaration.
struct Refusal {
    unsigned    line = 0;
    std::string reason;
};

struct Rewrite {
    std::vector<Edit>    edits;    // sorted by begin, non-overlapping
    std::vector<Refusal> refusals; // in source order
    unsigned             moved = 0;
};

// Plan the rewrite of the main file of `ast`. Only declarations in the main
// file are considered; included files are never edited.
Rewrite plan_rewrite(clang::ASTUnit& ast, const Options& options);

// A byte range of the converted text.
struct Span {
    unsigned begin                         = 0;
    unsigned end                           = 0;
    bool     operator==(const Span&) const = default;
};

// `source` with `edits` applied, and where each *insertion* (an edit with
// begin == end: a definition block, a section header) landed in the result.
// Pure: no Clang.
struct Applied {
    std::string       text;
    std::vector<Span> inserted;
};
Applied     apply_edits_tracked(std::string_view source, const std::vector<Edit>& edits);
std::string apply_edits(std::string_view source, const std::vector<Edit>& edits);

// `text` with the `.clang-format` found above `header_path` applied to the
// inserted spans only: what the tool wrote takes the header's own style, and
// what it did not touch keeps its bytes. Unchanged when no configuration is
// found above the header, or when formatting fails.
std::string format_inserted(std::string_view header_path, std::string text, const std::vector<Span>& inserted);

} // namespace beman::specgen::split_inline

#endif // BEMAN_SPECGEN_SPLIT_INLINE_REWRITE_HPP
