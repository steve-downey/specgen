// include/beman/specgen/backend/iref_spacing.hpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the space the draft's `\iref` sets before its parenthesis, for
// the backends that write the parenthesis themselves (issue #408).
//
// The draft writes `\iref` against the word before it, `is movable\iref{x}`,
// because the macro supplies the space: it prints "is movable (9.5.1)". The
// LaTeX backend writes the macro, so the space is the macro's. The mpark and
// org backends write the parenthesized reference where the macro stood, and
// so have to write the space too, where the text before it has none.
#ifndef BEMAN_SPECGEN_BACKEND_IREF_SPACING_HPP
#define BEMAN_SPECGEN_BACKEND_IREF_SPACING_HPP

#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace beman::specgen::backend {

// Whether a piece of prose begins with a parenthesized cross-reference, the
// draft's `\iref`; a bare `\ref` takes no parenthesis and no space.
inline bool opens_with_iref(const ir::PlainInline& piece) {
    const auto* ref = std::get_if<ir::RefInline>(&piece);
    return ref != nullptr && !ref->bare;
}

// A changed run begins with one when its first piece does.
inline bool opens_with_iref(const ir::Inline& piece) {
    if (const auto* ref = std::get_if<ir::RefInline>(&piece))
        return !ref->bare;
    const auto* run = std::get_if<ir::ChangedInline>(&piece);
    return run != nullptr && !run->inlines.empty() && opens_with_iref(run->inlines.front());
}

// The pieces rendered by `render` and joined, with a space before each that
// opens with a parenthesized cross-reference where the text before it does
// not already end in one. A reference that begins the text takes none.
template <typename Piece, typename Render>
std::string join_with_iref_spacing(const std::vector<Piece>& pieces, Render&& render) {
    return std::ranges::fold_left(pieces, std::string{}, [&render](std::string out, const Piece& piece) {
        if (opens_with_iref(piece) && !out.empty() && out.back() != ' ' && out.back() != '\n')
            out += ' ';
        return out + render(piece);
    });
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_IREF_SPACING_HPP
