// include/beman/specgen/frontend/draft_format.hpp                -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Normalization (design §3.6 step 2): code text reformatted in the draft's
// style through clang-format. The interface is strings; the FormatStyle stays
// inside, so only this component's .cpp reaches clang::format.

#ifndef BEMAN_SPECGEN_FRONTEND_DRAFT_FORMAT_HPP
#define BEMAN_SPECGEN_FRONTEND_DRAFT_FORMAT_HPP

#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace beman::specgen::frontend {

// The draft's line width, which a synopsis's final text has to respect too:
// enclose_in_namespace_std indents a synopsis's body this far *after* it is
// formatted, so a synopsis formats that much narrower (issue #157).
constexpr unsigned kDraftColumnLimit = 88;
constexpr unsigned kNamespaceIndent  = 2;

// `code` in the draft style, `indent` columns narrower than the draft's width
// for text that is indented that far afterwards.
//
// When the last line of a declaration lands exactly on the column limit,
// clang-format ends it there and puts the `;` on a line of its own rather than
// break the line earlier -- a requires-clause ending in a nested
// template-argument list, in practice (issue #157). A column narrower moves the
// break back inside the line, so the formatting is retried narrower until the
// lone `;` is gone. One that survives every retry is not that artifact but an
// empty statement, `while (f())` over an indented `;`, and the first
// formatting stands.
std::string format_draft(std::string_view code, unsigned indent);

// `indent` is how far the result is indented once formatted: a synopsis's
// kNamespaceIndent, else 0.
ir::CodeText format_and_recover(std::string                                            text,
                                const std::map<std::string, span_sentinels::SpanInfo>& sentinels,
                                std::optional<std::string_view>                        record_tag = std::nullopt,
                                unsigned                                               indent     = 0);

// Format an expression fragment in a declaration context, then take the
// context back off.
//
// clang-format parses what it is handed, and a bare fragment carries no
// context to parse in. From LLVM 23 on it reads the `&` of
// `requires(const Impl& impl) { ... }` as a binary operator rather than a
// declarator and spaces it as one, so a derived Constraints element came out
// saying `const Impl & impl` where the draft says `const Impl&` -- the very
// spelling draft_format_style() sets PAS_Left for. The draft's spelling is not
// negotiable against a formatter defect
// (docs/plans/llvm-23-port.md#constraints-fragment-spelling), so restore the
// context instead: wrapped as a variable initializer the fragment parses as
// what it is. An initializer and not a requires-clause because *any*
// expression is a valid one, and this path also carries Mandates conjuncts
// lifted from static_assert conditions, whose grammar a requires-clause need
// not admit.
//
// The wrapper comes off by its own fixed length, and ColumnLimit 0 is what
// makes that safe: with no limit clang-format honours the input's line breaks,
// so the added prefix reflows nothing after it and a multi-line fragment
// survives the round trip unchanged. Under LLVM 22, which formatted the bare
// fragment correctly, the whole thing is a no-op.
std::string format_expr_fragment(const std::string& text);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_DRAFT_FORMAT_HPP
