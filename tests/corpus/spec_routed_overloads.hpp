// tests/corpus/spec_routed_overloads.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_ROUTED_OVERLOADS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_ROUTED_OVERLOADS_HPP

namespace demo {

// \rSec2[routed.syn]{Header <routed> synopsis}

class gadget {
  public:
    // \ref{gadget.ctor}, constructors
    //! \group ctors
    //! \ensures Has no value.
    constexpr gadget() noexcept {}
    //! \returns Zero.
    int size() const { return 0; }
    //! \also ctors
    constexpr gadget(int) noexcept {}
    //! \also
    constexpr gadget(long) noexcept {}
    //!
    constexpr gadget(short) noexcept {}

    // \ref{gadget.obs}, observers
    //! \remarks The value type.
    using value_type = int;
    //! \also
    using reference = int&;

    //! \remarks The first constant.
    static constexpr int first = 1;
    //! \also
    static constexpr int second = 2;

    //! \effects Exchanges values.
    friend void swap(gadget&, gadget&) {}
    //! \also
    friend void swap(gadget&, int) {}
};

// \ref{gadget.obs}, namespace observers
//! \group observers
//! \returns Zero.
int observe(gadget&) { return 0; }
//! \also observers
int observe(gadget&, int) { return 0; }
//!
int observe(gadget&, long) { return 0; }

template <class T>
class specialized;

template <>
class specialized<int> {
  public:
    // \ref{gadget.ctor}, constructors
    //! \group specialized-ctors
    //! \ensures Has an integer value.
    specialized() {}
    //! \also specialized-ctors
    specialized(int) {}
};

template <>
class specialized<long> {
  public:
    // \ref{gadget.ctor}, constructors
    //! \group specialized-ctors
    //! \ensures Has a long value.
    specialized() {}
    //! \also specialized-ctors
    specialized(long) {}
};

/// END [routed.syn]

// \rSec3[gadget.ctor]{Constructors}
// \rSec3[gadget.obs]{Observers}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_ROUTED_OVERLOADS_HPP
