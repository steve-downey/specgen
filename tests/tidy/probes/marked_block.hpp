// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Marked, block form: the marker inside a contiguous comment run that ends
// on the line directly above the loop.
void block(int n) {
    // A reason that takes several lines to state, the way
    // backend/common.hpp writes it:
    // substrate generic algorithm: the marker may sit anywhere in the
    // block, and the block runs to the line directly above the loop.
    for (int i = 0; i != n; ++i) { }
}
