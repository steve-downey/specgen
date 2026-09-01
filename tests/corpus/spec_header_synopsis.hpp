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

//! \verbatim-synopsis
//! namespace std {
//!   template<class T> struct hash<demo::widget<T>>;
//! }

} // namespace demo

/// END [widget.syn]

// \rSec3[widget.ops]{Operations}
