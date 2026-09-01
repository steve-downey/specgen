// include/beman/specgen/foundation/parse/cursor.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Provenance: ported from compile-time-scheme
//   repo:   https://github.com/steve-downey/compile-time-scheme (commit 4b0b216)
//   path:   src/smd/smdscheme/parser/cursor.hpp
// Runtime-ized (decision parser-combinators): the upstream cursor is otherwise
// unchanged (it has no Capacity/static_vector use of its own — those live
// in the repetition combinators ported alongside parser.hpp). Adapted here by
// folding smdscheme::foundation::source_pos in directly (specgen has no
// separate foundation::source_pos of its own) and renaming the namespace to
// beman::specgen::foundation::parse. See foundation/DIVERGENCES.md.
#ifndef BEMAN_SPECGEN_FOUNDATION_PARSE_CURSOR_HPP
#define BEMAN_SPECGEN_FOUNDATION_PARSE_CURSOR_HPP

#include <cstddef>
#include <string_view>

namespace beman::specgen::foundation::parse {

/// A 1-based line/column position paired with a 0-based byte offset into the
/// original input, used to report parse failures and to drive line/column
/// tracking across newlines.
struct source_pos {
    std::size_t offset = 0; ///< 0-based byte offset from the start of input.
    int         line   = 1; ///< 1-based line number.
    int         column = 1; ///< 1-based column number.

    friend constexpr auto operator==(const source_pos&, const source_pos&) -> bool = default;
};

/// An immutable view into the remaining input with an associated source
/// position.
///
/// All advancing operations return a new @c cursor rather than mutating this
/// one, so parsers can checkpoint and backtrack freely (decision parser-combinators).
class cursor {
    std::string_view input_{};
    source_pos       pos_{};

  public:
    /// Constructs a cursor at the beginning of @p input.
    constexpr explicit cursor(std::string_view input) : input_{input} {}

    /// Returns true when no input remains.
    constexpr auto empty() const -> bool { return input_.empty(); }

    /// Returns the next character without consuming it.
    /// @pre !empty()
    constexpr auto peek() const -> char { return input_.front(); }

    /// Returns a new cursor that has consumed the next character, updating
    /// the position (line/column/offset).
    /// @pre !empty()
    constexpr auto bump() const -> cursor {
        cursor next{*this};
        if (!input_.empty()) {
            char c = input_.front();
            next.input_.remove_prefix(1);
            ++next.pos_.offset;
            if (c == '\n') {
                ++next.pos_.line;
                next.pos_.column = 1;
            } else {
                ++next.pos_.column;
            }
        }
        return next;
    }

    /// Returns the current source position.
    constexpr auto position() const -> source_pos { return pos_; }

    /// Returns the unconsumed portion of the input as a string_view.
    constexpr auto remaining() const -> std::string_view { return input_; }
};

/// Returns true if @p c is ASCII whitespace.
constexpr auto is_space(char c) -> bool { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/// Advances @p cur past all leading whitespace, returning the updated cursor.
constexpr auto skip_intertoken_space(cursor cur) -> cursor {
    while (!cur.empty() && is_space(cur.peek())) { // substrate generic algorithm
        cur = cur.bump();
    }
    return cur;
}

} // namespace beman::specgen::foundation::parse

#endif // BEMAN_SPECGEN_FOUNDATION_PARSE_CURSOR_HPP
