// tests/corpus/spec_inclass_bodies.hpp                            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Two in-class definitions with identical markup behave identically: a body
// is never synopsis content, whether or not it names an entity the drop set
// rewrites. The qualifier edit inside `scaled`'s body used to suppress the
// body splice through the overlap watermark, so the member whose body
// mentioned `std::` kept its implementation in the synopsis while its
// unqualified twin was reduced to a declaration. The `std` declaration is a
// local stand-in (the corpus is hermetic); only the resolution matters.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_BODIES_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_BODIES_HPP

namespace std {
inline constexpr int scale = 2;
} // namespace std

namespace demo {

struct widget {
    // \ref{demo.obs}, observers
    //! \returns A bigger value.
    auto local(int x) const { return x + 1; }

    //! \returns A scaled value.
    auto scaled(int x) const { return x * std::scale; }
};

// \rSec3[demo.obs]{Observers}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INCLASS_BODIES_HPP
