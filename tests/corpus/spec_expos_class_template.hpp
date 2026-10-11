// tests/corpus/spec_expos_class_template.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// `\expos` on a namespace-scope class template (issue #23): the definition
// renders as a free-standing exposition synopsis — name rewritten to its
// exposid, the draft's `// exposition only` comment appended, the record
// template-head line break kept — and every template-id use of it renders
// through the exposid, exactly the treatment alias templates already get.
// `\expos(name)` overrides the display name as it does for members. The
// marker used to be accepted with no effect and no diagnostic.
//
// A partial specialization follows its primary and needs no marker of its own
// (issue #49): it is the same entity, so it cannot be exposition-only under
// one name and not another. It used to render its raw name and no
// `// exposition only`, which is what an exposition-only helper usually looks
// like -- a primary and one specialization carrying the real case -- so the
// implementation spelling the marker exists to hide reached the wording, exit
// 0 and validation silent.
//
// `same_model_impl` is here for its *width*: a sentinel stands in for the
// exposition name only until recovery, but clang-format decides line breaks
// and continuation alignment while it is still there, so a name whose
// exposition spelling is a different width came out aligned to the sentinel's
// width instead of its own (issue #67). Its use below wraps, which is where a
// continuation lining up with nothing shows.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CLASS_TEMPLATE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_EXPOS_CLASS_TEMPLATE_HPP

namespace demo {

//! \expos
template <typename T>
struct box {
    using type = T;
};

template <typename T>
struct box<T*> {
    using type = T;
};

//! \expos(raw-box)
template <typename T>
struct raw_box_ {
    T value;
};

//! \expos
template <typename T>
inline constexpr bool boxed_ = false;

template <typename T>
inline constexpr bool boxed_<box<T>> = true;

//! \expos
template <typename LEFT, typename RIGHT, typename FALLBACK>
struct same_model_impl {
    static constexpr bool value = true;
};

//! \expos
template <typename MODEL_GRADE_PARAMETER, typename OPERAND_PARAMETER, typename FALLBACK_PARAMETER>
concept mixes_with_model = same_model_impl<MODEL_GRADE_PARAMETER, OPERAND_PARAMETER, FALLBACK_PARAMETER>::value;

template <typename...>
inline constexpr bool never_ = false;

// `\expos` changes how a defined class is named and nothing else: it is
// extracted the way any class is (design §3.4). An `\omit`ted member leaves
// with its body -- here one naming `never_`, which no wording documents --
// Doxygen and markup comments in the body are dropped, its members keep their
// wording, and the class meets the coverage rule like any other. It used to be
// copied through as a free-standing declaration, body, comments and `\omit`ted
// member included, so the helper leaked and validation failed.
//! \expos
template <typename T>
struct holder {
    /// Doxygen, which no synopsis keeps.
    using value_type = T;

    constexpr T get() const;

    constexpr bool empty() const;

    //! \omit
    template <typename... A>
    constexpr void refuse(A&&...) const {
        static_assert(never_<A...>, "refused");
    }
};

//! \returns The held value.
template <typename T>
constexpr T holder<T>::get() const {
    return T{};
}

//! \returns `false`.
template <typename T>
constexpr bool holder<T>::empty() const {
    return false;
}

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
