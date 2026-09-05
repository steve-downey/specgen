// tests/corpus/spec_expos_class_template.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\expos` on a namespace-scope class template (issue #23): the definition
// renders as a free-standing exposition synopsis — name rewritten to its
// exposid, the draft's `// exposition only` comment appended, the record
// template-head line break kept — and every template-id use of it renders
// through the exposid, exactly the treatment alias templates already get.
// `\expos(name)` overrides the display name as it does for members. The
// marker used to be accepted with no effect and no diagnostic.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CLASS_TEMPLATE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CLASS_TEMPLATE_HPP

namespace demo {

//! \expos
template <typename T>
struct box {
    using type = T;
};

//! \expos(raw-box)
template <typename T>
struct raw_box_ {
    T value;
};

//! \returns A boxed copy of `t`.
template <typename T>
constexpr box<T> make(T t) {
    return box<T>{};
}

//! \returns The raw form of `b`.
template <typename T>
constexpr raw_box_<T> unwrap(box<T> b) {
    return raw_box_<T>{};
}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CLASS_TEMPLATE_HPP
