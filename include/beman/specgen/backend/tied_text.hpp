// include/beman/specgen/backend/tied_text.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen -- running text with its ties written as a target's own no-break
// space, for the backends that do not write draft LaTeX.
#ifndef BEMAN_SPECGEN_BACKEND_TIED_TEXT_HPP
#define BEMAN_SPECGEN_BACKEND_TIED_TEXT_HPP

#include <beman/specgen/ir.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace beman::specgen::backend {

// The draft's line-break hints are LaTeX typesetting, and a backend that does
// not write LaTeX passes over them -- all but the tie. `subclause~\ref{x}`
// says the number stays on the line with the word before it, which is as true
// of a paper in HTML as of the draft, and every target has a way to say it.
// So the text of `v` comes back with each tied space written as `no_break`
// (`&nbsp;` in Markdown, `\nbsp{}` in org) and everything else as it is.
inline std::string tied_text(const ir::TextInline& v, std::string_view no_break) {
    std::string out;
    std::size_t pos = 0;
    // substrate generic algorithm: the position-tracking walk of
    // code_spans.hpp, over a hint table with no spans.
    for (const ir::LayoutHint& hint : v.hints) {
        if (hint.at < pos || !ir::tied_space(hint, v.text))
            continue;
        out += std::string_view(v.text).substr(pos, hint.at - pos);
        out += no_break;
        pos = hint.at + 1;
    }
    out += std::string_view(v.text).substr(pos);
    return out;
}

} // namespace beman::specgen::backend

#endif // BEMAN_SPECGEN_BACKEND_TIED_TEXT_HPP
