// tests/corpus/diff/optional_assign_asdraft.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus): two members of
// `optional`'s [optional.assign] whose descriptions are the working draft's,
// taken with `\asdraft` from tests/corpus/standard-slices/optional.assign.slice
// (docs/plans/draft-import.md). The assignment from `nullopt_t` carries no
// wording of its own. The converting assignment has a requires-clause, so its
// derived Constraints is the evidence the draft's four bullets are checked
// against; two of the draft's predicates are ones the code spells differently
// or not at all, and `\covers` says which are covered. A third, typed as a
// detached `\verbatim-itemdecl`, is matched to the draft's by its declaration.
//
// Self-contained: the traits are stub variable templates, so the header parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_DIFF_OPTIONAL_ASSIGN_ASDRAFT_HPP
#define BEMAN_SPECGEN_CORPUS_DIFF_OPTIONAL_ASSIGN_ASDRAFT_HPP

namespace demo {

template <class T, class U>
constexpr bool is_same_v = false;
template <class T>
constexpr bool is_scalar_v = false;
template <class T, class U>
constexpr bool is_constructible_v = true;
template <class T, class U>
constexpr bool is_assignable_v = true;
template <class T>
using remove_cv_t = T;
template <class T>
using remove_cvref_t = T;

struct nullopt_t {
    //! \omit
    explicit constexpr nullopt_t(int) {}
};

template <class T>
class optional {
  public:
    // \ref{optional.assign}, assignment
    constexpr optional<T>& operator=(nullopt_t) noexcept;

    template <class U = remove_cv_t<T>>
    constexpr optional& operator=(U&& v)
        requires(!is_same_v<remove_cvref_t<U>, optional>) &&
                (!(is_scalar_v<T> && is_same_v<T, U>)) && is_constructible_v<T, U> && is_assignable_v<T&, U>;

  private:
    bool engaged_ = false;
};

// \rSec3[optional.assign]{Assignment}

//! \asdraft
template <class T>
constexpr optional<T>& optional<T>::operator=(nullopt_t) noexcept {
    engaged_ = false;
    return *this;
}

//! \asdraft
//! \covers constraints !(is_scalar_v<T> && is_same_v<T, U>)
template <class T>
template <class U>
constexpr optional<T>& optional<T>::operator=(U&& v)
    requires(!is_same_v<remove_cvref_t<U>, optional>) &&
            (!(is_scalar_v<T> && is_same_v<T, U>)) && is_constructible_v<T, U> && is_assignable_v<T&, U>
{
    engaged_ = true;
    return *this;
}

// `emplace` is declared nowhere this document can see, so its item is typed as a
// detached `\verbatim-itemdecl`, which names no entity. It still takes the
// draft's description, matched by how its declaration reads. Detached items come
// after the members the class declares, which is the draft's order here.

//! \asdraft
//! \verbatim-itemdecl
//! template<class... Args> constexpr T& emplace(Args&&... args);

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_DIFF_OPTIONAL_ASSIGN_ASDRAFT_HPP
