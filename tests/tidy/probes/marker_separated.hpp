// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
void separated(int n) {
    // substrate generic algorithm: separated from its loop by a line of
    // code, so it marks nothing.
    int x = 0;
    for (int i = 0; i != n; ++i) { x += i; }
    (void)x;
}
