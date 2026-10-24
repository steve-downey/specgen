// include/beman/specgen/ir_json.hpp                                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the IR's JSON format (`--emit-ir`, `render --from-ir`): emission
// and parsing for the node types in ir.hpp, built on the generic JSON
// components in json/ (decision json-single-schema). The format is the
// frozen contract between the front end and everything downstream of it; the
// schema itself, one descriptor table per IR type, is in ir_json.cpp.

#ifndef BEMAN_SPECGEN_IR_JSON_HPP
#define BEMAN_SPECGEN_IR_JSON_HPP

#include <beman/specgen/ir.hpp>

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace beman::specgen::ir {

// --- serialization (--emit-ir) --------------------------------------------
// Emission returns the JSON rather than writing it into an ambient stream
// (decision format-print-output): the one caller that has a sink writes the
// result once, at the end. This mirrors `backend::latex::render_to_string`,
// and is the same "produce a value, write at the very end" shape
// backend/latex.cpp's prolog argues for. The *format* is the frozen contract,
// not this signature.
std::string emit_json(const Document&);
std::string emit_json(const Node&);
std::string emit_json(const SpecItem&);
std::string emit_json(const CodeText&);

// --- deserialization -------------------------------------------------------
// The IR is the contract between the clang-dependent front end and everything
// downstream, so the emitted form has to be readable back: it is what lets the
// external orgwg21 exporter and the backend goldens work without linking clang.
//
// Accepts the JSON these emitters produce. Key order is not significant and
// insignificant whitespace is permitted, so hand-edited goldens stay valid.
struct ParseError {
    std::size_t offset = 0; // byte offset into the input
    std::string message;
};

// A single fallible step (decision expected-error-taxonomy): JSON parsing is
// fail-fast, and in a serialization format the first error is definitive, so
// the taxonomy's carrier is `std::expected<T, ParseError>` plus
// `.and_then(...)`, not Validation-style accumulation (the taxonomy makes IR
// JSON parsing one of the two named fail-fast cases, alongside `parse_rsec`).
std::expected<Document, ParseError> parse_document(std::string_view);
std::expected<SpecItem, ParseError> parse_item(std::string_view);
std::expected<CodeText, ParseError> parse_code(std::string_view);

} // namespace beman::specgen::ir

#endif // BEMAN_SPECGEN_IR_JSON_HPP
