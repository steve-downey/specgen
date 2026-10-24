// include/beman/specgen/backend/element_label.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the draft's display label for each description element, for the
// backends that do not write draft LaTeX and so have no macro to carry it.
#ifndef BEMAN_SPECGEN_BACKEND_ELEMENT_LABEL_HPP
#define BEMAN_SPECGEN_BACKEND_ELEMENT_LABEL_HPP

#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace beman::specgen::backend {

// --- the draft's own name for a description element ------------------------
//
// [structure.specifications]'s label, as the working draft renders it:
// `\effects` sets "Effects:", `\expects` sets "Preconditions:", `\errors`
// sets "Error conditions:". Read off the draft's macros.tex (each element
// macro is `\Fundesc{<label>}`), not paraphrased.
//
// This is deliberately *not* `ir::element_name`, which spells the LaTeX macro
// (`"expects"`, `"errors"`) and is simultaneously the IR's JSON schema key --
// a frozen wire spelling that must not drift into a display concern. And it
// is deliberately not in the LaTeX backend either: that backend never needs a
// label, because the macro carries it, while every backend that is *not*
// writing draft LaTeX -- mpark, org -- needs this exact table.
constexpr std::array<std::pair<ir::ElementKind, std::string_view>, 13> kElementLabels{{
    {ir::ElementKind::Constraints, "Constraints"},
    {ir::ElementKind::Mandates, "Mandates"},
    {ir::ElementKind::Expects, "Preconditions"},
    {ir::ElementKind::HardExpects, "Hardened preconditions"},
    {ir::ElementKind::Effects, "Effects"},
    {ir::ElementKind::Sync, "Synchronization"},
    {ir::ElementKind::Ensures, "Postconditions"},
    {ir::ElementKind::Result, "Result"},
    {ir::ElementKind::Returns, "Returns"},
    {ir::ElementKind::Throws, "Throws"},
    {ir::ElementKind::Complexity, "Complexity"},
    {ir::ElementKind::Remarks, "Remarks"},
    {ir::ElementKind::Errors, "Error conditions"},
}};

constexpr std::string_view element_label(ir::ElementKind kind) {
    const auto it = std::ranges::find_if(kElementLabels, [kind](const auto& entry) { return entry.first == kind; });
    return it != kElementLabels.end() ? it->second : "?";
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_ELEMENT_LABEL_HPP
