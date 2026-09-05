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
struct sentinel {
    // \ref{widget.ops}, comparison
    //! \returns `t == 0`.
    template <class T>
    friend constexpr bool operator==(const T& t, sentinel) {
        return t == 0;
    }
};

//! \verbatim-synopsis
//! namespace std {
//!   template<class T> struct hash<demo::widget<T>>;
//! }

} // namespace demo

/// END [widget.syn]

// \rSec3[widget.ops]{Operations}
