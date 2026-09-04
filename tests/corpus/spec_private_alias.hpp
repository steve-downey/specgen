// tests/corpus/spec_private_alias.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A struct whose private alias is named by a public one. The private section
// is elided without leaving a stray leading `public:` (default member access
// is already public in a struct), and the surviving `using type = raw;`
// names a declaration the reader cannot see — published wording that will
// not compile if copied — so this header keeps its leak on purpose:
// `private_alias_validate` pins the Error and its fixit (mark `raw`
// `\expos`, or mask `type`'s RHS), the spec_namespace.hpp pattern.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_PRIVATE_ALIAS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_PRIVATE_ALIAS_HPP

namespace demo {

template <class T>
struct lifted {
  private:
    using raw = T;

  public:
    using type = raw;
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_PRIVATE_ALIAS_HPP
