// include/beman/specgen/frontend/expos_spellings.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Exposition-only uses by name (design §3.5).
// §3.5's stated exception to reference-resolved rewriting: "dependent member
// uses of the class's own expos members may fall back to name match within the
// class's fragments only". That is exactly this — the spellings are the class's
// own expos members, and the scan is confined to fragments extracted from that
// class (its equiv bodies and its prose), never the whole file.

#ifndef BEMAN_SPECGEN_FRONTEND_EXPOS_SPELLINGS_HPP
#define BEMAN_SPECGEN_FRONTEND_EXPOS_SPELLINGS_HPP

#include <beman/specgen/ir.hpp>
#include <beman/specgen/span_sentinels.hpp>

#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <string>
#include <vector>

namespace beman::specgen::frontend {

// `record`'s exposition-only members as written-spelling → exposid display name.
std::map<std::string, std::string> expos_spellings(const clang::CXXRecordDecl*                      record,
                                                   const std::map<const clang::Decl*, std::string>& expos_set);

bool is_ident_char(char c);

// Replace whole-identifier occurrences of an expos spelling with a sentinel, so
// format_and_recover turns each into an `\exposid` span. Whole-word only: a
// member named `val` must not rewrite the `val` inside `evaluate`.
std::string apply_expos_sentinels(std::string                                      text,
                                  const std::map<std::string, std::string>&        spellings,
                                  unsigned&                                        span_n,
                                  std::map<std::string, span_sentinels::SpanInfo>& sentinels);

// Design §7: a `\ref{stable.name}` in a synopsis group comment is a
// *cross-reference*, not code text, so it becomes an `ir::SpanKind::Ref` span
// here. Keeping the draft's own `// \ref{optional.ctor},
// constructors` verbatim would leave a per-backend escape sitting in the IR,
// which design §7 forbids in as many words — LaTeX
// passing through a LaTeX backend is indistinguishable from correct output,
// while markdown would show a LaTeX macro in a ```cpp fence.
//
// Restricted to the tail of a `//` comment, which is both where the draft
// writes one and what the enumerator's own documentation says it is. Scanning
// the whole fragment would be shorter and would *invent* a span for a `\ref{`
// inside a string literal -- the wrong direction for imprecision to run, by
// the leakage checker's own discipline. Two known gaps, neither reached
// by any corpus header: a `\ref` in a `/* */` comment is not converted (the
// only `/*!` form a synopsis sees is a docblock, stripped before this runs),
// and one split across a line break is left alone.
std::string
apply_ref_sentinels(std::string text, unsigned& span_n, std::map<std::string, span_sentinels::SpanInfo>& sentinels);

// Rewrite expos spellings inside a description's inline code spans (design §4.1:
// "backticked spans are reference-resolved: expos names render as \exposid").
// A `` `value_` `` in prose becomes an ExposId span carrying the exposid name,
// so the backend emits \exposid{value} rather than \tcode{value_}.
void rewrite_prose_expos(beman::specgen::ir::ItemDescr& descr, const std::map<std::string, std::string>& spellings);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_EXPOS_SPELLINGS_HPP
