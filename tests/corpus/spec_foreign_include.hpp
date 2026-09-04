// tests/corpus/spec_foreign_include.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (design §9: the leakage checker's qualifier
// half). The `demo::detail` machinery lives in an *included* header — the
// tidy refactor that used to disable the check — so the surviving `detail::`
// qualifier must still be recorded as foreign and reported. The
// `std::ranges` stand-in lives in the same include: its qualifier renders as
// `ranges::` and must stay silent, because the standard's own vocabulary
// belongs in wording wherever it happens to be declared. This header keeps
// its leak on purpose; `foreign_include_validate` pins the findings it
// draws, the spec_namespace.hpp pattern.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_FOREIGN_INCLUDE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_FOREIGN_INCLUDE_HPP

#include "support/spec_foreign_detail.hpp"

namespace demo {

template <class Impl>
struct widget {
    // \ref{demo.obs}, observers
    auto probe(int x) const
        requires requires(const Impl& impl) { impl.step(detail::eval); };

    std::ranges::probe_t<int> slot() const;
};

// \rSec3[demo.obs]{Observers}

//! \returns Something.
template <class Impl>
auto widget<Impl>::probe(int x) const
    requires requires(const Impl& impl) { impl.step(detail::eval); }
{
    return x;
}

//! \returns An empty probe.
template <class Impl>
std::ranges::probe_t<int> widget<Impl>::slot() const {
    return {};
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_FOREIGN_INCLUDE_HPP
