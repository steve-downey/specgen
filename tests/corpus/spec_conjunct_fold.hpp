// tests/corpus/spec_conjunct_fold.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A derived conjunct is inline code, so a clause clang-format broke across
// lines is folded onto one (design §5.1). Each newline used to become a space,
// so a break just inside a bracket left `convertible_to< T, int>` (issue
// #167). Now a break after an opening `(` or template `<`, or before a closing
// one, folds to nothing, and any other break, such as one after the binary `<`
// in `h`'s Mandates, still folds to a single space. `k`'s conjunct renders as
// `X models C`, where `X` is a template-id broken after its `<` and closed by
// the concept's `>>` (issue #168).

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_CONJUNCT_FOLD_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_CONJUNCT_FOLD_HPP

namespace demo {

template <class T, class U>
concept interconvertible_with_every_other_argument_of_the_same_rather_long_family_of_argument_types_too = true;

template <class T>
concept holdable = true;

template <class FUNCTION, class... ARGUMENTS>
using call_result_t = FUNCTION;

template <class T>
using wrapped_reference_to_an_argument_with_a_rather_long_descriptive_name_t = T&;

template <class T, class U, class V>
constexpr bool all_three_of_these_types_are_layout_compatible_with_each_other_in_turn(T*, U*, V*) {
    return true;
}

// \rSec2[demo.fold]{Folded conjuncts}

//! \effects Does nothing.
template <class FIRST_ARGUMENT_TYPE, class SECOND_ARGUMENT_TYPE>
    requires interconvertible_with_every_other_argument_of_the_same_rather_long_family_of_argument_types_too<
                 FIRST_ARGUMENT_TYPE,
                 SECOND_ARGUMENT_TYPE> &&
             (all_three_of_these_types_are_layout_compatible_with_each_other_in_turn(
                 static_cast<FIRST_ARGUMENT_TYPE*>(nullptr),
                 static_cast<SECOND_ARGUMENT_TYPE*>(nullptr),
                 static_cast<FIRST_ARGUMENT_TYPE*>(nullptr)))
void g(FIRST_ARGUMENT_TYPE, SECOND_ARGUMENT_TYPE) {}

//! \effects Does nothing.
template <class FIRST_ARGUMENT_TYPE, class SECOND_ARGUMENT_TYPE>
void h(FIRST_ARGUMENT_TYPE, SECOND_ARGUMENT_TYPE) {
    static_assert(sizeof(FIRST_ARGUMENT_TYPE) + sizeof(SECOND_ARGUMENT_TYPE) + sizeof(FIRST_ARGUMENT_TYPE) <
                  sizeof(SECOND_ARGUMENT_TYPE) + sizeof(FIRST_ARGUMENT_TYPE) * 64 + sizeof(SECOND_ARGUMENT_TYPE));
}

//! \effects Does nothing.
template <class FUNCTION_TYPE, class FIRST_ARGUMENT_TYPE, class SECOND_ARGUMENT_TYPE>
    requires holdable<FUNCTION_TYPE> &&
             holdable<call_result_t<
                 FUNCTION_TYPE&,
                 wrapped_reference_to_an_argument_with_a_rather_long_descriptive_name_t<FIRST_ARGUMENT_TYPE>,
                 wrapped_reference_to_an_argument_with_a_rather_long_descriptive_name_t<SECOND_ARGUMENT_TYPE>>>
void k(FUNCTION_TYPE, FIRST_ARGUMENT_TYPE, SECOND_ARGUMENT_TYPE) {}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_CONJUNCT_FOLD_HPP
