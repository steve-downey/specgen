// include/beman/specgen/fragments.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — splitting a document into transcludable fragments.
// Design §8 asks every backend for "standalone fragments for transclusion"
// whose "paths derive from stable names (optional.ctor.tex|md|org), default
// split granularity per \rSec3". That is one rule about *documents*, not three
// rules about targets: the stable name is IR data and the split is IR surgery,
// so both live here, in Tier A, and each piece is rendered by the ordinary
// backend entry point afterwards. The extension is the only per-backend part,
// and it belongs to whoever chose the backend.
// See docs/architecture.md §8.

#ifndef BEMAN_SPECGEN_FRAGMENTS_HPP
#define BEMAN_SPECGEN_FRAGMENTS_HPP

#include <beman/specgen/ir.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::fragments {

// One output file's worth of wording: the stable name its path derives from,
// and the nodes it owns as a document in its own right.
//
// A fragment is a whole ir::Document rather than a node list so that rendering
// one is the same call as rendering the whole (`render_to_string(document)`),
// with no fragment-shaped entry point in any backend. That is what makes the
// per-backend framing come out right by construction: mpark wraps each
// top-level node in a `::: wording` div, and a fragment holding one section is
// one div, which is where a paragraph counter should restart anyway.
//
// The document-level validator channels (`foreign_namespaces`,
// `unextracted_uses`) are deliberately *not* carried across. They describe the
// header, not any one section of it, so splitting them would either duplicate
// or arbitrarily assign them; a caller that validates does so over the whole
// document, before splitting.
struct Fragment {
    std::string  name;     // "optional.ctor" — no extension, no directory
    ir::Document document; // rendered by any backend as an ordinary document
};

struct Options {
    // The name of the fragment holding top-level nodes that sit outside every
    // section — the class synopses, in every corpus header. Empty means derive
    // it: see `split` below.
    std::string root;
};

// Why a document could not be split. `root_unnamed` distinguishes the one
// failure a caller can fix by supplying `Options::root`, so a driver can name
// its own flag in the message without this header knowing what that flag is
// called.
struct Error {
    std::string message;
    bool        root_unnamed = false;
};

// Split a document at its top-level sections, one fragment each, in document
// order. Under the LaTeX backend's default `base_section_depth` a top-level
// ir::Section renders as `\rSec3`, which is design §8's default granularity;
// splitting at any other depth would leave a section's own heading in one
// fragment and its children in others, and is left for a corpus that wants it.
//
// Every top-level node that is *not* a section (a class synopsis, a free
// paragraph) joins one root fragment, placed where the first of them appears.
// Its name comes from `Options::root`, or, when that is empty, from the longest
// dotted prefix the section names share — `optional.ctor`, `optional.assign`,
// … all yield `optional`. A prefix equal to one of those names (which is what
// a lone section gives) is shortened by one component, since a fragment cannot
// share a path with another.
//
// Fails, rather than writing something surprising, when a section has no
// stable name, when two fragments would claim one name, when a name cannot be
// a file name (`usable_as_file_name` below), or when there are loose nodes and
// no name can be derived for them.
std::expected<std::vector<Fragment>, Error> split(const ir::Document&, const Options& = {});

// Whether a fragment name can be a file name as it stands. Stable names come
// out of a header comment, so this is a boundary check and not a formality: it
// admits the dotted-identifier shape the draft uses and nothing else — no
// separator, no leading dot or dash, no `..` — which is what keeps a path from
// naming somewhere other than the directory it was written into.
//
// The name is used unchanged when it passes; there is deliberately no
// sanitizing rewrite, because a fragment whose path does not read as its
// stable name is worse than a refusal to write it.
bool usable_as_file_name(std::string_view);

} // namespace beman::specgen::fragments

#endif // BEMAN_SPECGEN_FRAGMENTS_HPP
