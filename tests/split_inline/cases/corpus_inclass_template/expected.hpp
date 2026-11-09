// tests/corpus/spec_inclass_template.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_TEMPLATE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_TEMPLATE_HPP

namespace demo {

class converter {
  public:
    // \ref{converter.ops}, operations
    template <class T>
    int convert(T value);

    /** @brief Constructs an empty converter. */
    converter(int);

    /** @brief A merged implementation overload. */
    template <class T>
    void merged(T);
};

// \rSec3[converter.ops]{Operations}

//! \effects Returns `value` converted to `int`.
template <class T>
int converter::convert(T value) {
    return static_cast<int>(value);
}

//! \merge
inline converter::converter(int) {}

//! \merge
template <class T>
void converter::merged(T) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_TEMPLATE_HPP
