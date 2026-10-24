// include/beman/specgen/diagnostic.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the one severity vocabulary shared by the docblock grammar
// (grammar::Diagnostic, docblock.hpp) and the wording validators
// (validate::Diagnostic, validate/validate.hpp). Shared rather than
// duplicated: both diagnostic shapes rank findings
// the same way, and a second `enum class Severity` would let the two
// vocabularies drift.
#ifndef BEMAN_SPECGEN_DIAGNOSTIC_HPP
#define BEMAN_SPECGEN_DIAGNOSTIC_HPP

#include <array>

#include <beman/specgen/foundation/enumeration.hpp>

namespace beman::specgen {

enum class Severity { Error, Warning, Note };

} // namespace beman::specgen

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::Severity> = std::array{
    beman::specgen::Severity::Error,
    beman::specgen::Severity::Warning,
    beman::specgen::Severity::Note,
};

namespace beman::specgen {

// The one spelling of a severity in user-facing output, shared by every
// printer: validate::format_diagnostic renders a validator finding
// with it, and the driver renders a document_build::Diagnostic with it. A
// printer that hand-writes the three words instead of deriving them from the
// severity can drift — as far as `generate` printing a docblock Error as
// `warning:`.
constexpr const char* severity_label(Severity severity) {
    // A total table: a fourth severity that is not listed here fails to
    // compile, instead of printing as a note.
    static constexpr auto labels = foundation::table<Severity>(foundation::is<Severity::Error>("error"),
                                                               foundation::is<Severity::Warning>("warning"),
                                                               foundation::is<Severity::Note>("note"));
    return labels[severity];
}

} // namespace beman::specgen

#endif // BEMAN_SPECGEN_DIAGNOSTIC_HPP
