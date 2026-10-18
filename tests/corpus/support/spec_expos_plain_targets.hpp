// tests/corpus/support/spec_expos_plain_targets.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Support header for spec_expos_plain_class.hpp (issue #142): the
// non-template implementation types a library keeps in a header of its own,
// and reaches from its specified header only through `\expos`
// using-declarations. Declared outside the main file on purpose: a type the
// rewrite missed would surface as `detail::` leakage, and an enumerator of
// the enumeration as a foreign name.

#ifndef BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_EXPOS_PLAIN_TARGETS_HPP
#define BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_EXPOS_PLAIN_TARGETS_HPP

namespace demo::impl {
struct helper_t {
    int value;
};
using count_t = unsigned;
enum class mode_t { fast, slow };
} // namespace demo::impl

#endif // BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_EXPOS_PLAIN_TARGETS_HPP
