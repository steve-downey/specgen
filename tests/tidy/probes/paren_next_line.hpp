// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// A `for` whose parenthesis sits on the following line: the text gate's
// recorded miss, which the AST sees like any other loop.
void split(int n) {
    for
        (int i = 0; i != n; ++i) { }
}
