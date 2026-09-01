// tests/corpus/include_path/specgen_acid/widget_traits.hpp         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Sibling-directory header for the `-I` golden (tests/golden/CMakeLists.txt,
// case `include_path`): the real acid target, `beman/optional/optional.hpp`,
// reaches `stl_interfaces` through exactly this shape -- an angle include of
// a header one directory over (`<beman/optional/detail/iterator.hpp>`) --
// which is a shape the front end can only parse when handed an `-I` to
// pass through. This header is the minimal analogue of that
// shape, not a copy of it.
//
// It lives beside `consumer/`, not inside it, and that is load-bearing: a
// header reachable via a path relative to the *including* file's own
// directory would resolve even with a plain angle include, because Clang's
// recovery for a failed `<angle>` search silently retries the same spelling
// as a `"quoted"` search relative to the includer before giving up (verified
// against clang++ 22 directly: with this header living beside
// spec_include.hpp instead of beside `consumer/`, the include still resolved
// with no `-I` at all, and the diagnostic said so: "file not found with
// <angled> include; use \"quotes\" instead"). Putting `consumer/` between
// them means that fallback looks in `consumer/specgen_acid/`, finds nothing,
// and the failure is a genuine `fatal error` that halts preprocessing of the
// rest of the file -- which is what makes spec_include.hpp's golden
// load-bearing rather than accidentally satisfiable without the `-I` the
// command line supplies.
//
// Self-contained itself, so the only thing that can make this header
// unreachable is the missing include path in the corpus header that reaches
// for it.

#ifndef BEMAN_SPECGEN_CORPUS_INCLUDE_PATH_SPECGEN_ACID_WIDGET_TRAITS_HPP
#define BEMAN_SPECGEN_CORPUS_INCLUDE_PATH_SPECGEN_ACID_WIDGET_TRAITS_HPP

struct widget_traits {
    using value_type = int;
};

#endif // BEMAN_SPECGEN_CORPUS_INCLUDE_PATH_SPECGEN_ACID_WIDGET_TRAITS_HPP
