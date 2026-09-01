// include/beman/specgen/backend/org.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — org-mode serializer.
// Emits org for the `wg21org` exporter (ox-wg21latex.el / ox-wg21html.el):
// org headings carrying a stable name, org-native wording prose
// (`/Effects/: `, `~code~`), and code in `#+begin_codeblock` /
// `#+begin_itemdecl` **special blocks** — which org exports to the draft's own
// `lstnewenvironment`s, so the draft's `@...@` escape convention applies
// inside them verbatim. Like the other two backends these are standalone
// fragments for transclusion; the including paper owns its front matter and
// framing. See docs/architecture.md §8.
//
// Design §8 says of this backend that "correctness is defined by the
// exporter", so its conventions are settled against wg21org rather than
// chosen here; org.cpp records each one where it happens.

#ifndef BEMAN_SPECGEN_BACKEND_ORG_HPP
#define BEMAN_SPECGEN_BACKEND_ORG_HPP

#include <beman/specgen/ir.hpp>

#include <string>

namespace beman::specgen::backend::org {

struct Options {
    // Org outline level of a top-level Section; nested sections descend from
    // here. Two by default, so a fragment's sections sit under the including
    // paper's own `* Wording` heading — the shape `view-maybe.org` uses, and
    // the counterpart of mpark's `base_heading_level` rather than of LaTeX's
    // `base_section_depth` (`\rSec3` is a *draft* split granularity; a paper's
    // wording starts a level or two below its title).
    //
    // Unlike the mpark backend there is no cap: markdown stops at six heading
    // levels and org does not.
    int base_heading_level = 2;
};

// Rendering returns the fragment; a caller that has a sink writes it once
// (decision format-print-output — the boundary rule all three backends follow).
std::string render_to_string(const ir::Document&, const Options& = {});
std::string render_to_string(const ir::SpecItem&, const Options& = {});

} // namespace beman::specgen::backend::org

#endif // BEMAN_SPECGEN_BACKEND_ORG_HPP
