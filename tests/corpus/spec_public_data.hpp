// tests/corpus/spec_public_data.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A public data member is specification (issue #82).  Its type and its name
// are in the synopsis and its class's own description says what it means,
// which is how the draft specifies `from_chars_result`,
// `ranges::in_out_result` and every other such struct: a data member's
// specification *is* its declaration.
//
// Both §9 checks used to say otherwise -- coverage reported it undescribed,
// which is a description that cannot exist, since a docblock on a data member
// produces no wording; and naming it in the class's own paragraph was a second
// finding, on the grounds that it is not a documented entity.  A *private*
// member is unchanged: it is exposition, and `\expos` is what says so.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_PUBLIC_DATA_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_PUBLIC_DATA_HPP

namespace demo {

// \rSec2[demo.result]{Class `result`}

//! \remarks `code_point` is the decoded value, and `is_error` says whether the
//! decode failed; when it did, `code_point` is U+FFFD.
struct result {
    char32_t code_point{};
    bool     is_error{false};

    //! \expos
    int scratch_{0};
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_PUBLIC_DATA_HPP
