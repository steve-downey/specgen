// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// A loop a macro produces is a site at the macro's *use* -- invisible to the
// text gate entirely.
#define REPEAT3(body) for (int i = 0; i != 3; ++i) { body; }
void expanded() {
    int n = 0;
    REPEAT3(++n)
    (void)n;
}
