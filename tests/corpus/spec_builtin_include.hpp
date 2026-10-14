// tests/corpus/spec_builtin_include.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus). The one corpus header
// with an #include: <stddef.h> is one of Clang's builtin headers, which specgen
// carries beside its own binary (the resource directory, lib/clang/<major>), so
// reaching it reaches nothing outside what specgen ships. The golden parses it
// with `-nostdlibinc`, which takes away the host's own include directories. It
// does not take away a fallback a distribution's libclang-cpp bakes in (Debian's
// /usr/include/clang/<version>/include survives it), so on such a machine this
// case passes even if the driver loses its resource directory; the
// driver.resource-dir case, which reads back what Clang was told, is the one
// that fails there.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_BUILTIN_INCLUDE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_BUILTIN_INCLUDE_HPP

#include <stddef.h>

namespace demo {

class buffer {
  public:
    // \ref{buffer.observers}, observers
    size_t size() const;

  private:
    size_t size_ = 0;
};

// \rSec3[buffer.observers]{Observers}

//! \returns The number of bytes held.
size_t buffer::size() const { return size_; }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_BUILTIN_INCLUDE_HPP
