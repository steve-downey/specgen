// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// All four unmarked loop forms are sites.
void forms(int n) {
    for (int i = 0; i != n; ++i) { }
    int a[3] = {1, 2, 3};
    for (int value : a) { (void)value; }
    while (n != 0) { --n; }
    do { ++n; } while (n != 3);
}
