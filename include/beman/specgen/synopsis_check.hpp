// include/beman/specgen/synopsis_check.hpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — a synopsis that agrees with the code (docs/plans/draft-import.md,
// part 4).
//
// A `\verbatim-synopsis`, or a synopsis inherited from the draft, is text, and
// nothing ties it to the declarations it stands for. The front end keeps the
// synopsis the code itself generates (`Synopsis::declared`) beside it, and this
// pairs the two member declarations by name and then by how alike they read,
// and compares what a paper changes without touching a word of wording:
// `constexpr`, `explicit`, `noexcept`, `static`, `friend`, `= default` and
// `= delete`. Clang-free, like `diff`: it reads text, as the differ does.
#ifndef BEMAN_SPECGEN_SYNOPSIS_CHECK_HPP
#define BEMAN_SPECGEN_SYNOPSIS_CHECK_HPP

#include <beman/specgen/ir.hpp>

#include <string>
#include <vector>

namespace beman::specgen::synopsis_check {

struct Finding {
    std::string declaration; // the synopsis's declaration, on one line; the code's when the synopsis lacks it
    std::string message;
};

// Where the synopsis and the declarations disagree: a paired declaration whose
// specifiers differ, and a declaration only one side has. Empty when they agree.
std::vector<Finding> compare(const ir::CodeText& synopsis, const ir::CodeText& declared);

struct Patched {
    ir::CodeText         code;
    std::vector<Finding> unpatched; // what remains, which `compare` of the result reports too
};

// `synopsis` with the specifiers of each paired declaration written as
// `declared` has them, so the draft's layout, placeholders and comments stay
// and only what the paper changed is different. `constexpr`, `explicit` and
// `noexcept` are written; the rest are reported in `unpatched`, as is a
// declaration only one side has.
Patched patch(const ir::CodeText& synopsis, const ir::CodeText& declared);

} // namespace beman::specgen::synopsis_check

#endif // BEMAN_SPECGEN_SYNOPSIS_CHECK_HPP
