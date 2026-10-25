// include/beman/specgen/section_markers.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the two plain-comment markers that shape a document besides
// `\rSec` (rsec.hpp): the Doxygen `/// END [stable]` fence that closes a
// section, and the `// \ref{stable}` comment that names a synopsis group
// inside a class body. Both are scans over a comment's raw text, so the
// front end reads them through this header and needs no Clang to do it.

#ifndef BEMAN_SPECGEN_SECTION_MARKERS_HPP
#define BEMAN_SPECGEN_SECTION_MARKERS_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace beman::specgen::grammar {

struct SectionEndFence {
    std::string stable;
    std::size_t line_begin = 0;
};

// An END fence is deliberately Doxygen, not a specgen marker: it bounds a
// section without becoming authored wording. Clang can merge a `.syn` fence
// into the RawComment that carries a terminal \verbatim-synopsis, so return
// byte offsets as well as the stable name.
std::optional<SectionEndFence> section_end_fence(std::string_view raw);

// Recognize a `\ref{<stable>}` synopsis-group comment inside a class body
// (design §3.2) and return its stable name. The Beman form is
// `// \ref{stable.name}, human label`; only the stable name is needed here, to
// route an in-class-defined member's itemdescr to the matching `\rSec` section.
// Same tolerant, decoration-aware scan as parse_rsec; anything that is
// not a `\ref{...}` comment yields std::nullopt.
std::optional<std::string> parse_ref(std::string_view raw);

} // namespace beman::specgen::grammar

#endif // BEMAN_SPECGEN_SECTION_MARKERS_HPP
