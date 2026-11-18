// tests/corpus/spec_macro_specifiers.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Specifiers spelled through macros (issue #349): one that expands to a
// keyword, one to nothing, one to another such macro, and one that is not a
// specifier. Each is written as it expands, in the synopsis and in the item
// declaration, except the last, which is left as written. A macro first in a
// declaration, or in its definition, leaves the item where the declaration is
// written (issue #348), and so does one last. A declaration a function-like
// macro stamps out, its name and all, is the macro's, and no wording.

#define DEMO_CONSTEXPR constexpr
#define DEMO_NOTHING
#define DEMO_INDIRECT DEMO_CONSTEXPR
#define DEMO_NOEXCEPT noexcept
#define DEMO_VALUE 42
#define DEMO_SPECIALIZE(T) \
    template <>            \
    struct box<T> {}

namespace demo {
// \rSec2[w.thing]{Class `thing`}
//! A thing.
class thing {
  public:
    inline DEMO_CONSTEXPR int f() const;
    inline DEMO_NOTHING int   g() const;
    DEMO_INDIRECT int         h() const;
    DEMO_NOTHING int          k() const;
    int                       m() const DEMO_NOEXCEPT;
    int                       v(int x = DEMO_VALUE) const;
};

//! \returns 1.
inline DEMO_CONSTEXPR int thing::f() const { return 1; }
//! \returns 2.
inline DEMO_NOTHING int thing::g() const { return 2; }
//! \returns 3.
DEMO_INDIRECT int thing::h() const { return 3; }
//! \returns 4.
inline int thing::k() const { return 4; }
//! \returns 5.
inline int thing::m() const DEMO_NOEXCEPT { return 5; }
//! \returns `x`.
inline int thing::v(int x) const { return x; }

// \rSec2[w.box]{Class template `box`}
//! A box.
template <class T>
struct box {};

DEMO_SPECIALIZE(int);
} // namespace demo
