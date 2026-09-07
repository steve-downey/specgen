// tests/corpus/spec_document_span_diagnostics.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A finding in a followed header names that header, not this one (issue #77):
// the line is a line of the file the markup is in, and reporting it against
// the umbrella would name a line in a file that has no such line.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DOCUMENT_SPAN_DIAGNOSTICS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DOCUMENT_SPAN_DIAGNOSTICS_HPP

// \rSec2[spanbad.syn]{Header `<spanbad>` synopsis}

#include "document_span_part.hpp"

/// END [spanbad.syn]

// \rSec2[spanbad.vocab]{Vocabulary}

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DOCUMENT_SPAN_DIAGNOSTICS_HPP
