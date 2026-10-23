// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// The marker on the switch line, or in the comment block directly above it.
enum class Color { Red, Green };

int trailing(Color c) {
    switch (c) { // enumeration eliminator: probe, trailing form.
    case Color::Red:
        return 0;
    case Color::Green:
        return 1;
    }
    return -1;
}

int block(Color c) {
    // enumeration eliminator: probe, block form.
    switch (c) {
    case Color::Red:
        return 0;
    case Color::Green:
        return 1;
    }
    return -1;
}
