// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// substrate generic algorithm: a marker above the enclosing function marks
// nothing inside it; each loop must carry its own.
void three(int n) {
    for (int i = 0; i != n; ++i) { }
    while (n != 0) { --n; }
    do { ++n; } while (n != 3);
}
