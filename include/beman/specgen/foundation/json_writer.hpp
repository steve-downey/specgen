// include/beman/specgen/foundation/json_writer.hpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// RAII object/array scopes that own JSON comma placement (decision
// json-single-schema). A hand-threaded `bool first` flag with a
// `std::exchange(first, false)` test at every call site that decides when a
// separating comma is due is loop-carried
// state a caller can get wrong (skip an element, forget to reset the flag
// between sibling lists, ...). Here the comma is a side effect of asking a
// scope object for "the next thing to write" (`key()`/`element()`), and the
// closing bracket is a side effect of the scope's destructor — so nesting is
// expressed by C++ scope lifetime, not by a hand-tracked flag or an explicit
// close() call, and a forgotten comma or a mismatched bracket is not
// expressible.
//
// This header owns punctuation, nothing else. Callers
// still decide what keys exist and in what order — the schema-describing
// descriptor tables that also generate that shape live in
// json_descriptor.hpp, not this header.
//
// The sink is a `std::string&`, not a `std::ostream&` (decision
// format-print-output): emission produces a value and the one caller that
// has a stream writes it once, at the end — the same shape as
// backend/latex.cpp. A concrete
// string rather than a template on an output iterator, because there is
// exactly one sink and genericity here would buy nothing but instantiations.
#ifndef BEMAN_SPECGEN_FOUNDATION_JSON_WRITER_HPP
#define BEMAN_SPECGEN_FOUNDATION_JSON_WRITER_HPP

#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace beman::specgen::foundation {

/// Appends @p s to @p out as a quoted, escaped JSON string.
///
/// The escaping here is part of the IR JSON format, which is a frozen
/// contract: these exact bytes are what the goldens pin, so the logic is
/// preserved, not re-derived. One quirk existing goldens depend on: a control
/// character below 0x20 with no named escape (`\"`, `\\`, `\n`, `\t`, `\r`)
/// is written as `\u00XX` with **lowercase** hex digits. The JSON grammar
/// accepts either case, but goldens were captured with lowercase, so this is
/// preserved exactly rather than normalized — `{:02x}` is the lowercase
/// spelling (decision format-print-output).
inline void write_json_string(std::string_view s, std::string& out) {
    out += '"';
    // substrate generic algorithm -- character-level escaping appended
    // straight to the sink: it produces no value and accumulates nothing of
    // its own, so there is no fold or transform underneath it. Its bytes are
    // also the frozen contract noted above, a second reason not to
    // restructure it.
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                std::format_to(std::back_inserter(out), "\\u00{:02x}", static_cast<unsigned char>(c));
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

/// RAII scope for a JSON array: appends '[' on construction, ']' on
/// destruction. Call element() once immediately before writing each
/// element's value; it returns the sink to append that value to, having
/// already appended the separating comma if this is not the first element.
/// Not copyable or movable — a scope is a place in the output, not a value
/// to hand around.
class json_array {
  public:
    explicit json_array(std::string& out) : out_(&out) { *out_ += '['; }
    ~json_array() { *out_ += ']'; }

    json_array(const json_array&)            = delete;
    json_array& operator=(const json_array&) = delete;
    json_array(json_array&&)                 = delete;
    json_array& operator=(json_array&&)      = delete;

    /// Returns the sink to append the next element's value to.
    std::string& element() {
        if (!std::exchange(first_, false)) {
            *out_ += ',';
        }
        return *out_;
    }

  private:
    std::string* out_;
    bool         first_ = true;
};

/// RAII scope for a JSON object: appends '{' on construction, '}' on
/// destruction. Call key(name) once per member, immediately before writing
/// that member's value; it returns the sink to append the value to, having
/// already appended the separating comma (if this is not the first member),
/// the quoted member name, and the ':'. Not copyable or movable, for the
/// same reason as json_array.
class json_object {
  public:
    explicit json_object(std::string& out) : out_(&out) { *out_ += '{'; }
    ~json_object() { *out_ += '}'; }

    json_object(const json_object&)            = delete;
    json_object& operator=(const json_object&) = delete;
    json_object(json_object&&)                 = delete;
    json_object& operator=(json_object&&)      = delete;

    /// Returns the sink to append this member's value to.
    std::string& key(std::string_view name) {
        if (!std::exchange(first_, false)) {
            *out_ += ',';
        }
        write_json_string(name, *out_);
        *out_ += ':';
        return *out_;
    }

  private:
    std::string* out_;
    bool         first_ = true;
};

} // namespace beman::specgen::foundation

#endif // BEMAN_SPECGEN_FOUNDATION_JSON_WRITER_HPP
