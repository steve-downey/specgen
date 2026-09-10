// tests/corpus/support/spec_shared_spelling_tables.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Support header for spec_shared_spelling.hpp (design §9): the generated
// lookup tables a real library keeps out of the header it specifies, each
// named after the encoding it decodes. `windows_1252` here and
// `demo::codec::windows_1252` there are different entities sharing a
// spelling on purpose — the table is named after the encoding, and so is the
// enumerator — and only this one is declared outside the document.
//
// Nothing here is marked `\expos`, and nothing here should have to be
// (decision shared-spelling-foreign-name): a table the document never
// reaches is not an exposition-only entity *of the specification*, and
// saying so in thirty generated headers to quiet a text match would put a
// false claim in every one of them.

#ifndef BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_SHARED_SPELLING_TABLES_HPP
#define BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_SHARED_SPELLING_TABLES_HPP

namespace demo {
namespace detail {
namespace tables {
inline constexpr int windows_1252[4] = {0, 1, 2, 3};
} // namespace tables
} // namespace detail
} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SUPPORT_SPEC_SHARED_SPELLING_TABLES_HPP
