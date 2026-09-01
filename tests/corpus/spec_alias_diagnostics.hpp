// tests/corpus/spec_alias_diagnostics.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

namespace demo {
class invalid_markers {
  public:
    //! \impdef
    //! \effects Does nothing.
    void f() {}

    //! \seebelow noexcept
    //! \remarks The alias has an invalid targeted marker.
    using type = int;
};
} // namespace demo
