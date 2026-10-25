// include/beman/specgen/frontend/crash_handler.hpp               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// LLVM's crash handler, installed by the driver with a banner naming the
// build. It lives in the front-end library because that is the one place
// LLVM is reachable from (decision ir-boundary).

#ifndef BEMAN_SPECGEN_FRONTEND_CRASH_HANDLER_HPP
#define BEMAN_SPECGEN_FRONTEND_CRASH_HANDLER_HPP

#include <string>
#include <string_view>

namespace beman::specgen::frontend {

// Install LLVM's crash handler for the life of the process: on a fatal signal,
// write `banner` to stderr and then LLVM's stack trace. Called once, first
// thing, by the driver. A release binary is fully stripped, so its trace is a
// list of `specgen(+0x...)` offsets; the banner says which build those offsets
// belong to, which is what makes the release's separate debug-information
// asset usable against them.
void install_crash_handler(std::string_view argv0, std::string banner);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_CRASH_HANDLER_HPP
