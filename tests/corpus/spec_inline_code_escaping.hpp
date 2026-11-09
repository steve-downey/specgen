// tests/corpus/spec_inline_code_escaping.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_INLINE_CODE_ESCAPING_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_INLINE_CODE_ESCAPING_HPP

namespace demo {

// \rSec3[escaping.general]{General}

//! A reference qualifier is one of the following:
//! \item `&`, or
//! \item `&&`.
//!
//! Special characters in code are literal: `# % _ { } ~ ^ $ \`.

// \rSec3[escaping.references]{References}

//! \returns `value`.
//! \remarks A reference is written `int&`.
//! \libtab2[escaping.tokens]{Reference tokens `&` and `&&`}
//! \column Token
//! \column Meaning & spelling
//! \row `&&`
//! \cell The tokens `&` & `&&` denote reference qualifiers.
//! \endlibtab2
inline int& alias(int& value) { return value; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_INLINE_CODE_ESCAPING_HPP
