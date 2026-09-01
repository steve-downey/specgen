// installtest/consumer.cpp                                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Consumes the installed package the way an outside project would: include a
// header from the installed FILE_SET and call into the installed library.
// Deliberately tiny -- this test is about the install being complete and
// usable, not about behaviour, which the main suite covers.

#include <beman/specgen/ir.hpp>

#include <print>
#include <string_view>

int main() {
    namespace ir = beman::specgen::ir;

    // element_name spells the IR's frozen JSON keys, so it is a stable thing
    // for a consumer to assert on.
    if (ir::element_name(ir::ElementKind::Effects) != std::string_view{"effects"}) {
        std::println(stderr, "installed beman::specgen returned an unexpected element name");
        return 1;
    }

    std::println("beman.specgen install test: library OK");
    return 0;
}
