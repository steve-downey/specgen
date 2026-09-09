// tests/corpus/spec_merge_deleted_template.hpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A `\merge`d deleted (or defaulted) function *template* must disappear
// from the synopsis whole, the same way a non-template one does (issue
// #85). Clang's own parser extends a deleted/defaulted function's recorded
// end location past the `= delete`/`= default` keyword only when the
// declarator Sema handed back is itself a FunctionDecl (ParseCXXInlineMethods.cpp);
// for a function template that declarator is the FunctionTemplateDecl, the
// cast fails silently, and the templated FunctionDecl's own end is left at
// the parameter list's closing `)` -- never advanced to the keyword at
// all. Trusting that end location left a dangling `= delete;` fragment with
// no declarator behind, invalid C++ in the rendered synopsis.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_MERGE_DELETED_TEMPLATE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_MERGE_DELETED_TEMPLATE_HPP

namespace demo {

// \rSec2[demo.widget]{Widget}

//! \remarks A widget.
template <class T>
class widget {
  public:
    //! \at demo.widget
    //! \effects Does the thing.
    constexpr explicit widget(int g) {}

    //! \merge
    template <class G>
    requires(sizeof(G) > 0)
    constexpr widget(G) = delete;

  private:
    T v;
};

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_MERGE_DELETED_TEMPLATE_HPP
