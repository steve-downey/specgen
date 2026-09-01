// tests/corpus/spec_attach.hpp                                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.3: redeclaration-chain
// attachment). Exercises overload disambiguation: `top()` and `top() const`
// are declared in-class and defined out-of-line under one `\rSec3` section,
// each with its own `//!` docblock — the itemdecl for each SpecItem must come
// from its own in-class declaration, unqualified, and must not be confused
// with its overload sibling. `push` rounds out the section with a plain
// (non-overloaded) out-of-line member and an `\effects` docblock.
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_ATTACH_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_ATTACH_HPP

namespace demo {

class stack {
  public:
    // \ref{stack.access}, element access
    int&       top();
    const int& top() const;
    void       push(int value);

  private:
    int data_ = 0;
};

// \rSec3[stack.access]{Element access}

//! \returns A reference to the top element.
int& stack::top() { return data_; }

//! \returns A reference to the top element.
const int& stack::top() const { return data_; }

//! \effects Adds `value` to the stack.
void stack::push(int value) { data_ = value; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_ATTACH_HPP
