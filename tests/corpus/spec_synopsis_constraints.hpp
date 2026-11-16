// tests/corpus/spec_synopsis_constraints.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A synopsis drops a requires-clause wherever the item declaration drops it
// (issue #281). The clause is removed from an item declaration and stated as
// *Constraints*; the class synopsis used to keep it, so the synopsis and the
// item declaration of the same member disagreed, and the draft's synopses do
// not repeat a clause its *Constraints* paragraph states: [expected.object.general]
// declares `template<class U, class G> constexpr explicit(see below)
// expected(const expected<U, G>&);` and [expected.object.cons] has the
// constraints.
//
// One member for each way the rule comes out:
//
//  - the converting constructor: a clause on the template head, stated as
//    *Constraints*, gone from the synopsis;
//  - `assign`: a trailing clause, the same;
//  - `reset`: `\constraints-in-decl` keeps the clause in the item declaration,
//    so the synopsis keeps it too, and the two agree;
//  - the copy constructor: `= default` with a clause and no description. It
//    has no item, so nothing else would state the clause, and it stays;
//  - `swap`: a hidden friend, whose markup is in the class, like any member;
//  - `exchange`: a namespace-scope function in the header synopsis, on the
//    same rule.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_CONSTRAINTS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_CONSTRAINTS_HPP

namespace demo {

template <class T, class U>
inline constexpr bool convertible_like = true;
template <class T>
inline constexpr bool copyable_like = true;
template <class T>
inline constexpr bool resettable_like = true;

// \rSec2[demo.cell.syn]{Header synopsis}

template <class T>
class cell;

template <class T>
    requires copyable_like<T>
void exchange(cell<T>& a, cell<T>& b);

/// END [demo.cell.syn]

// \rSec2[demo.cell]{Class template cell}

template <class T>
class cell {
  public:
    // \ref{demo.cell.cons}, constructors
    template <class U>
        requires(convertible_like<U, T> && !convertible_like<U, cell>)
    explicit cell(U&& u);

    cell(const cell&)
        requires copyable_like<T>
    = default;

    // \ref{demo.cell.mod}, modifiers
    void assign(const T& v)
        requires copyable_like<T>;

    void reset()
        requires resettable_like<T>;

    //! \effects Exchanges the values of `a` and `b`.
    friend void swap(cell& a, cell& b)
        requires copyable_like<T>
    {
        a.assign(b.value_);
    }

  private:
    //! \expos
    T value_;
};

// \rSec3[demo.cell.cons]{Constructors}

//! \effects Initializes the value with `u`.
template <class T>
template <class U>
    requires(convertible_like<U, T> && !convertible_like<U, cell<T>>)
cell<T>::cell(U&& u) : value_(u) {}

// \rSec3[demo.cell.mod]{Modifiers}

//! \effects Assigns `v` to the value.
template <class T>
void cell<T>::assign(const T& v)
    requires copyable_like<T>
{
    value_ = v;
}

//! \constraints-in-decl
//! \effects Resets the value.
template <class T>
void cell<T>::reset()
    requires resettable_like<T>
{}

//! \effects Exchanges the values of `a` and `b`.
template <class T>
    requires copyable_like<T>
void exchange(cell<T>& a, cell<T>& b) {
    swap(a, b);
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_CONSTRAINTS_HPP
