// tests/corpus/spec_codeblock_escape.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CODEBLOCK_ESCAPE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CODEBLOCK_ESCAPE_HPP

#include <functional>
#include <utility>

namespace demo {

// \rSec3[box.class]{Class template box}

//! An authored code block names an exposition-only member with the draft's
//! escape, which every backend writes in its own form (issue #406).
template <class T>
class box {
  public:
    // \ref{box.ops}, operations
    template <class F>
    constexpr void show(F&& f) const;

  private:
    //! \expos(val)
    T* value_ = nullptr;
};

// \rSec3[box.ops]{Operations}

//! \mandates The declaration
//! \codeblock
//! auto u(invoke(std::forward<F>(f), *@\exposid{val}@));
//! \endcodeblock
//! is well-formed.
//! \effects Calls `invoke(std::forward<F>(f), *val)`.
template <class T>
template <class F>
constexpr void box<T>::show(F&& f) const {
    std::invoke(std::forward<F>(f), *value_);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CODEBLOCK_ESCAPE_HPP
