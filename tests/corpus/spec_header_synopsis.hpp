// tests/corpus/spec_header_synopsis.hpp                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// \rSec2[widget.syn]{Header <widget> synopsis}

namespace demo {

struct tag {};
inline constexpr tag value{};

//! \omit
void omitted_helper();

//! \merge
void merged_helper();

/// API documentation is not draft wording.
template <class T>
class widget;

//! \expos widget-like
template <class T>
concept widget_like = true;

// \ref{widget.ops}, operations
template <class T>
void swap(widget<T>&, widget<T>&);

template <class T>
bool operator==(const widget<T>&, const widget<T>&);

// A class *defined* inside the region gathers its synopsis like any other
// declaration, but its `\ref`-routed in-class member belongs to the section
// that `\ref` names, not to the synopsis — the whole point of gathering is
// that the members live somewhere else. The fold used to take the class's
// code and drop the routed members riding with it, leaving the target
// section empty and saying nothing (issue #34).
//
// Its own wording is the other thing that used to go: the class-general
// paragraph its `static_assert` derives and the description its docblock
// carries have no route, so they belong beside its synopsis in the section
// that is open — this one (issue #41).
//! \remarks Comparing against `sentinel` never allocates.
struct sentinel {
    static_assert(sizeof(char) == 1);

    // \ref{widget.ops}, comparison
    //! \returns `t == 0`.
    template <class T>
    friend constexpr bool operator==(const T& t, sentinel) {
        return t == 0;
    }
};

// A customization point object belongs in the header synopsis, so a masked
// variable is inside the region by construction -- which is exactly where the
// mask stopped being applied (issue #55). The declaration folded in verbatim,
// initializer and `detail::` and all, and the leak was then reported against
// the marked declaration.
namespace detail {
//! \omit
struct adaptor {};
} // namespace detail

//! \seebelow
inline constexpr detail::adaptor cpo{};

//! \verbatim-synopsis
//! namespace std {
//!   template<class T> struct hash<demo::widget<T>>;
//! }

} // namespace demo

/// END [widget.syn]

// \rSec3[widget.ops]{Operations}
