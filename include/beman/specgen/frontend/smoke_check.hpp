// include/beman/specgen/frontend/smoke_check.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The link probe for the Clang front end: parse a snippet into an AST and
// reformat a fixed one through clang-format, which proves both LLVM surfaces
// the front end rests on are in the binary and run. The driver runs it when
// it is given no header.

#ifndef BEMAN_SPECGEN_FRONTEND_SMOKE_CHECK_HPP
#define BEMAN_SPECGEN_FRONTEND_SMOKE_CHECK_HPP

#include <string>
#include <string_view>

namespace beman::specgen::frontend {

// Result of smoke_check: whether Clang parsed the source into an AST,
// and the text clang::format produced for a fixed snippet. Exercising both
// surfaces is the point — the tooling front end (design §3.1) and the
// formatting normalizer (design §3.6) are the two LLVM dependencies the tier
// rests on, so the smoke test asserts both link and run.
struct SmokeResult {
    bool        ast_built = false;
    std::string formatted;
};

// Parse `source` as C++26 into a Clang AST and reformat a fixed snippet through
// clang-format's LLVM style. Proves the Tier B toolchain is wired end to end;
// carries no wording-generation logic.
SmokeResult smoke_check(std::string_view source);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_SMOKE_CHECK_HPP
