// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// A loop in a template used twice is one site, not one per instantiation.
template <typename T>
T sum3(T v) {
    T r{};
    for (int i = 0; i != 3; ++i) { r = r + v; }
    return r;
}
void uses() {
    (void)sum3(1);
    (void)sum3(1.5);
}
