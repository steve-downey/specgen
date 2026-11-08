// tests/corpus/spec_recommended.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_RECOMMENDED_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_RECOMMENDED_HPP

namespace demo {

// \rSec3[recommended.class]{Class template wrapper}

//! \recommended Implementations should avoid allocating memory for small values.
template <class T>
class wrapper {};

// \rSec3[recommended.reset]{Reset}

//! \effects Discards the stored value.
//! \remarks The wrapper may be reused.
//! \errors An error condition is reported by the caller.
//! \recommended Implementations should reuse allocated storage.
inline void reset() {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_RECOMMENDED_HPP
