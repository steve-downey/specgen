// tests/corpus/spec_authored_code_body_uses.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_CODE_BODY_USES_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_CODE_BODY_USES_HPP

namespace demo {

class wrapper {
    void hidden_() {}

  public:
    // \ref{authored.body}, operations
    //! \effects Performs the operation.
    //! \example A related invocation:
    //! \codeblock
    //! wrapper w;
    //! w.run();
    //! \endcodeblock
    //! \endexample
    void run() { hidden_(); }
};

// \rSec3[authored.body]{Operations}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_AUTHORED_CODE_BODY_USES_HPP
