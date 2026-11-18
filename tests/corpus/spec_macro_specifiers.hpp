// tests/corpus/spec_macro_specifiers.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Specifiers spelled through macros (issue #349): one that expands to a
// keyword, one to nothing, one to another such macro, and one that is not a
// specifier. Each is written as it expands, in the synopsis and in the item
// declaration, except the last, which is left as written. An empty macro may
// come first; one that expands to something first in a declaration is #348's.

#define DEMO_CONSTEXPR constexpr
#define DEMO_NOTHING
#define DEMO_INDIRECT DEMO_CONSTEXPR
#define DEMO_VALUE 42

namespace demo {
// \rSec2[w.thing]{Class `thing`}
//! A thing.
class thing {
  public:
    inline DEMO_CONSTEXPR int f() const;
    inline DEMO_NOTHING int   g() const;
    inline DEMO_INDIRECT int  h() const;
    DEMO_NOTHING int          k() const;
    int                       v(int x = DEMO_VALUE) const;
};

//! \returns 1.
inline DEMO_CONSTEXPR int thing::f() const { return 1; }
//! \returns 2.
inline DEMO_NOTHING int thing::g() const { return 2; }
//! \returns 3.
inline DEMO_INDIRECT int thing::h() const { return 3; }
//! \returns 4.
inline int thing::k() const { return 4; }
//! \returns `x`.
inline int thing::v(int x) const { return x; }
} // namespace demo
