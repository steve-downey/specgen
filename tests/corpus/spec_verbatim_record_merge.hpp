// tests/corpus/spec_verbatim_record_merge.hpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_RECORD_MERGE_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_RECORD_MERGE_HPP

#include "support/spec_hash_types.hpp"

// \rSec2[optional.hash]{Hash support}

//! \remarks The specialization is enabled when `hash<T>` is enabled.
//! \verbatim-itemdecl
//! template<class T> struct hash<optional<T>>;

namespace fixture {

//! \merge
template <class T>
struct hash<optional<T>> {
    static_assert(sizeof(T) > 0);

    unsigned operator()(const optional<T>&) const { return 0; }
};

} // namespace fixture

#endif // BEMAN_SPECGEN_CORPUS_SPEC_VERBATIM_RECORD_MERGE_HPP
