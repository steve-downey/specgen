// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Marked, trailing-comment form: the marker on the loop line itself.
void trailing(int n) {
    for (int i = 0; i != n; ++i) { } // substrate generic algorithm: trailing form.
    while (n != 0) { --n; }          // substrate generic algorithm: trailing form.
}
