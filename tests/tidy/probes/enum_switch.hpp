// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// A switch over an enumeration is a finding, whatever its fallback, and a
// default: label does not excuse it.
enum class Color { Red, Green };
enum Plain { A, B };
typedef Color Hue;

int named(Color c) {
    switch (c) {
    case Color::Red:
        return 0;
    case Color::Green:
        return 1;
    }
    return -1;
}

int through_typedef(const Hue h) {
    switch (h) {
    case Color::Red:
        return 0;
    default:
        return 1;
    }
}

int unscoped(Plain p) {
    switch (p) {
    case A:
        return 0;
    case B:
        return 1;
    }
    return -1;
}
