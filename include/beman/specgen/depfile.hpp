// include/beman/specgen/depfile.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the Makefile dependency fragment.
//
// A generated clause is a build artifact of the headers it was extracted from,
// and a paper repository that cannot say so rebuilds its PDF from stale
// wording without a murmur. `format` writes what a compiler's `-MD -MP` writes:
// one rule naming every file specgen produced and every file its parse read,
// then one empty rule per prerequisite.
//
// Tier A, and pure string manipulation with no IR and no Clang: *which* files
// the parse read is the front end's answer (frontend::DocumentBuild::sources)
// and which files were written is the driver's, but make's escaping and line
// continuations are neither, and they are the part worth a unit test.

#ifndef BEMAN_SPECGEN_DEPFILE_HPP
#define BEMAN_SPECGEN_DEPFILE_HPP

#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::depfile {

// Escape one path so make reads it as a single file name: a space becomes
// `\ `, a `#` becomes `\#` (make's comment character), and a `$` becomes `$$`
// (make's expansion sigil, which a literal dollar in a file name is not).
//
// Deliberately not escaped: a backslash, which on the platforms that use it as
// a separator is not an escape; and a colon, which make has no way to accept
// inside a prerequisite at all, so doubling it would turn an unrepresentable
// path into a silently wrong one rather than a visible failure.
std::string escape(std::string_view path);

// The fragment: every target, then every prerequisite, then the `-MP` block.
//
//     wording/a.tex wording/b.tex: include/a.hpp \
//       include/detail/b.hpp
//
//     include/a.hpp:
//     include/detail/b.hpp:
//
// The empty rules are why `-MP` exists and why this emits them unconditionally:
// without them, deleting or renaming a header makes every later build fail with
// "no rule to make target" — naming a prerequisite that the stale fragment still
// remembers — instead of simply regenerating. The cost is a rule per header; the
// alternative is a build that has to be repaired by hand.
//
// Paths are emitted exactly as given, relative or absolute: the caller resolved
// them against the directory make will run in, and rewriting them here could
// only guess at that.
//
// Empty `targets` yields an empty string. A rule with no target is not a rule,
// and writing a file holding only the `-MP` block would declare every header
// phony — which is to say, would quietly stop the paper from ever rebuilding.
std::string format(const std::vector<std::string>& targets, const std::vector<std::string>& prerequisites);

} // namespace beman::specgen::depfile

#endif // BEMAN_SPECGEN_DEPFILE_HPP
