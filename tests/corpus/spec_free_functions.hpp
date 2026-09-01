// tests/corpus/spec_free_functions.hpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Documented namespace-scope definitions are wording items, while an
// unannotated implementation helper remains absent.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_FREE_FUNCTIONS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_FREE_FUNCTIONS_HPP

namespace demo {

constexpr int inspect(int declared_value);

// \rSec2[free.functions]{Free functions}

//! \effects Returns its argument unchanged.
template <class T>
constexpr T identity(T value) {
    return value;
}

constexpr int implementation_helper(int value) { return value + 1; }

//! \returns-equiv
constexpr int inspect(int definition_value) { return definition_value; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_FREE_FUNCTIONS_HPP
