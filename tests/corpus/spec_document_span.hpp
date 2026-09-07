// tests/corpus/spec_document_span.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A document that spans the headers it includes (issue #77). The gathered
// `.syn` region is the author's statement of which includes are the header's
// specification surface: the two inside it are followed, and `detail.hpp`,
// outside it, is implementation and stays invisible.
//
// The clause sections live here, after the fence, and the followed headers
// route their wording to them by `\ref` group header — the same routing a
// class's members and a namespace entity already use inside a region.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DOCUMENT_SPAN_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DOCUMENT_SPAN_HPP

// \rSec2[span.syn]{Header `<span>` synopsis}

#include <document/errors.hpp>
#include <document/widget.hpp>

/// END [span.syn]

#include <document/detail.hpp>

// \rSec2[span.errors]{Error types}

// \rSec2[span.widget]{Class `widget`}

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DOCUMENT_SPAN_HPP
