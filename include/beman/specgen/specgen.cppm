export module beman.specgen;

import std;

#define BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT
export {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#include <beman/specgen/specgen.hpp>
#pragma clang diagnostic pop
}
