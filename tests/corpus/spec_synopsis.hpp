// tests/corpus/spec_synopsis.hpp                                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §3.4: subtractive synopsis
// extraction). Exercises every subtractive edit in one small class: a
// `= default` and a `= delete` member (left intact), a
// draft-form `\ref` comment (kept verbatim), and an in-class-defined hidden
// friend carrying a `//!` docblock (docblock stripped, body spliced to `;`).
// The defaulted copy constructor is nontrivial and ODR-used by the hidden
// friend, so Clang synthesizes a body for it; that body is not authored source
// and must not eat the last character of `default`.
// Both `\ref` groups have a matching `\rSec3` below, and every declaration is
// described, `= default`, or `= delete`, so the header satisfies the
// coverage invariant (design §9) — `specgen render --validate` is silent on it.
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HPP

namespace demo {

class number {
  public:
    // \ref{number.cons}, constructors
    number();
    //! \describe
    //! \effects Copy-constructs the stored value.
    number(const number&)            = default;
    number& operator=(const number&) = delete;

    //! \omit
    virtual ~number() = default;

    // \ref{number.observers}, observers
    int value() const;

    //! \returns `true` if the two numbers compare equal.
    friend bool operator==(number a, number b) { return a.value() == b.value(); }

  private:
    int value_ = 0;
};

// \rSec3[number.cons]{Constructors}

//! \effects Constructs a `number` holding zero.
number::number() : value_(0) {}

// \rSec3[number.observers]{Observers}

//! \effects Returns the stored value.
int number::value() const { return value_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_HPP
