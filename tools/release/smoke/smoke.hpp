// tools/release/smoke/smoke.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The release binary's smoke input (tools/release/smoke.sh). Unlike the test
// corpus, which is hermetic, this header reaches outside what specgen ships on
// purpose: <cstddef> is the host's C++ standard library, which a downloaded
// specgen finds through Clang's GCC-installation detection, and <optional>
// pulls in enough of it that a libstdc++ newer or older than the one the
// binary was built against would show up here first.

#ifndef BEMAN_SPECGEN_RELEASE_SMOKE_HPP
#define BEMAN_SPECGEN_RELEASE_SMOKE_HPP

#include <cstddef>
#include <optional>

namespace demo {

class slot {
  public:
    // \ref{slot.observers}, observers
    std::optional<std::size_t> index() const noexcept;

  private:
    std::optional<std::size_t> index_;
};

// \rSec3[slot.observers]{Observers}

//! \returns The index held, if any.
std::optional<std::size_t> slot::index() const noexcept { return index_; }

} // namespace demo

#endif // BEMAN_SPECGEN_RELEASE_SMOKE_HPP
