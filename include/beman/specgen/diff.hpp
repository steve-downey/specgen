// include/beman/specgen/diff.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the wording diff (docs/plans/wording-diff.md, decision
// ir-level-diff): two IR documents in, the current one marked with what the
// revision added and removed out. Structural where the IR has structure --
// sections match by stable name, synopses by class name, items by what they
// declare, elements by kind -- and a sequence diff (diff/sequence.hpp) only
// where the content is a sequence: paragraphs, words, code lines and tokens.
// Clang-free (Tier A): the baseline is a document however it was obtained.

#ifndef BEMAN_SPECGEN_DIFF_HPP
#define BEMAN_SPECGEN_DIFF_HPP

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include <beman/specgen/foundation/enumeration.hpp>
#include <beman/specgen/ir.hpp>

namespace beman::specgen::diff {

// How much unchanged wording the result keeps (`--diff-context`).
//
// `Changed` drops a top-level node with no change anywhere in it and keeps a
// changed one whole, unchanged paragraphs included: the shape LWG reads, the
// full subclause as modified. `All` keeps everything, for the paper that has
// to show nothing else moved. `Items` goes one step past `Changed`: inside a
// changed section, each maximal run of unchanged children collapses to one
// elided paragraph, the `[...]` a paper writes for wording left out, and an
// unchanged subsection, at any depth, keeps its heading over one such
// paragraph, so a `\ref` to it resolves and its siblings number as in the
// draft. `Decls` goes one step past `Items`: an item whose declaration
// changed but whose description did not (a paper that adds `constexpr` or
// `noexcept` across an interface) keeps the declaration and shows its
// description as one elision. An item whose description changed renders it
// whole, as under `Items`.
enum class Context { Changed, All, Items, Decls };

} // namespace beman::specgen::diff

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::diff::Context> = std::array{
    beman::specgen::diff::Context::Changed,
    beman::specgen::diff::Context::All,
    beman::specgen::diff::Context::Items,
    beman::specgen::diff::Context::Decls,
};

namespace beman::specgen::diff {

std::string_view       context_name(Context); // "changed", "all", "items", "decls"
std::optional<Context> context_from_name(std::string_view);

struct Options {
    Context context = Context::Changed;
    // Two paragraphs, two code lines, or two list items are the same one
    // edited when at least this share of their words (Jaccard over the token
    // sets) is common; below it they are a removal and an addition. The
    // plan's first cut, signed off on the corpus pair's goldens.
    double pairing_threshold = 0.5;
    // A run of at most this many unchanged words or tokens wedged between two
    // changes joins them (diff::join_islands), so a replaced phrase reads as
    // one replacement.
    std::size_t island = 2;
};

// The current document, marked. A node, element or block the baseline lacks
// is `Added`; one the current lacks is carried over from the baseline and
// `Removed`; a pair is recursed into, down to runs of words and code tokens.
// Both inputs are expected canonical and unmarked (`has_marks` below): a diff
// of a diff means nothing, and the driver refuses it.
//
// Three laws the tests pin: `compare(d, d)` under `Context::All` is `d`;
// `compare({}, d)` marks every top-level node `Added` and nothing beneath;
// and stripping the `Removed` material and the marks from `compare(a, b)`
// gives `b`, while stripping the `Added` material gives `a`.
ir::Document compare(const ir::Document& baseline, const ir::Document& current, const Options& = {});

// Whether any mark but `Same` sits anywhere in the document: on a node, an
// element, a block, a run of prose or a run of code.
bool has_marks(const ir::Document&);

} // namespace beman::specgen::diff

#endif // BEMAN_SPECGEN_DIFF_HPP
