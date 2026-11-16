// tests/corpus/spec_nested_class_members.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The function members of a nested class follow the synopsis rules a direct
// member follows (issue #298). A nested class that stays in its enclosing
// class's synopsis as a definition used to be carried as written, so inside
// it:
//
//  - `advance()` kept the requires-clause its item declaration drops and
//    states as *Constraints*, the disagreement issue #281 removed for direct
//    members;
//  - `get()` kept its body, which is never synopsis content;
//  - `hidden()` stayed although it is `\omit`ted;
//  - `reset(double)` printed its P2573 message.
//
// `cursor` is nested in `iterator`, to show the rules reach any depth. An
// exposition-only nested class shown as a definition, `sentinel`, is on the
// same path.
//
// What stays the enclosing class's alone: access labels, `\ref` groups and
// the private-member rule. `steps_` is a private member of a nested class and
// is still printed.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_NESTED_CLASS_MEMBERS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_NESTED_CLASS_MEMBERS_HPP

namespace demo {

template <class T>
inline constexpr bool steppable = true;

template <class T>
class range {
  public:
    // \ref{demo.range.iter}, class range::iterator
    class iterator {
      public:
        void advance()
            requires steppable<T>;

        int get() const { return steps_; }

        //! \omit
        void hidden();

        void reset(double) = delete ("range: no fractional steps");

        class cursor {
          public:
            int at() const { return 0; }
        };

      private:
        int steps_ = 0;
    };

    //! \expos
    class sentinel {
      public:
        bool reached() const { return true; }
    };

    // \ref{demo.range.obs}, observers
    iterator begin() const;
};

// \rSec3[demo.range.iter]{Class range::iterator}

//! \effects Advances the iterator by one step.
template <class T>
void range<T>::iterator::advance()
    requires steppable<T>
{
    ++steps_;
}

// \rSec3[demo.range.obs]{Observers}

//! \returns An iterator to the first element.
template <class T>
typename range<T>::iterator range<T>::begin() const {
    return {};
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_NESTED_CLASS_MEMBERS_HPP
