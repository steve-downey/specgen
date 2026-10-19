// tests/corpus/spec_source_markup.cc                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The definitions of spec_source_markup.hpp's members, whose markup is read
// with `--source` (issue #149).

#include "spec_source_markup.hpp"

namespace demo {

//! \effects-equiv
void tally::reset() { count_ = 0; }

//! \effects Sets the count to `value`.
void tally::assign(long value) {
    static_assert(sizeof(long) >= sizeof(int));
    count_ = static_cast<int>(value);
}

//! \omit
void tally::rehash() {}

//! \returns-expr
int tally::size() const { return count_; }

//! \seebelow
//! \returns A pointer to the count.
int* tally::data() { return &count_; }

//! \returns `x.size() == y.size()`.
bool operator==(const tally& x, const tally& y) { return x.count_ == y.count_; }

} // namespace demo
