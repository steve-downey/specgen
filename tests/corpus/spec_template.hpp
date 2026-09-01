// tests/corpus/spec_template.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.6: normalization). Exercises
// the draft FormatStyle's template/requires/concept options that the other
// corpus headers never touch: a namespace-scope concept, a class template
// constrained by a requires-clause (extracted starting at the `template`
// keyword, design §3.6 checkpoint), and an out-of-line template member
// definition also carrying a requires-clause. Self-contained (no #includes)
// and built from bool/a template parameter only, so it parses standalone
// under -std=c++2c without ever instantiating the template.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_TEMPLATE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_TEMPLATE_HPP

namespace demo {

template <class T>
concept regular = requires(T a, T b) { a == b; };

template <class T>
    requires regular<T>
class box {
  public:
    // \ref{box.observers}, observers
    T get() const;

  private:
    T value_;
};

// \rSec3[box.observers]{Observers}

//! \effects Returns the stored value.
template <class T>
    requires regular<T>
T box<T>::get() const {
    return value_;
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_TEMPLATE_HPP
