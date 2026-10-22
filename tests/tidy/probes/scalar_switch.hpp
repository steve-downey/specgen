// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// A switch over a scalar is outside the rule: its summands are not an
// interface anyone maintains.
int on_char(char c) {
    switch (c) {
    case 'a':
        return 0;
    default:
        return 1;
    }
}

int on_int(int n) {
    switch (n) {
    case 0:
        return 0;
    default:
        return 1;
    }
}
