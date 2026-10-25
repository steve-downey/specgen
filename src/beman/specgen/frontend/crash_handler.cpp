// src/beman/specgen/frontend/crash_handler.cpp                   -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/crash_handler.hpp>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Signals.h>

#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>

namespace beman::specgen::frontend {

void install_crash_handler(std::string_view argv0, std::string banner) {
    // Owned for the life of the process, because the handler reads it from a
    // signal context at any point after this returns; written with write(2),
    // the one output call that is safe there.
    static std::string crash_banner;
    crash_banner = std::move(banner);
    // Registered first, so it runs ahead of the stack trace, which
    // PrintStackTraceOnErrorSignal adds as a handler of its own.
    llvm::sys::AddSignalHandler(
        [](void*) {
            [[maybe_unused]] const auto written = ::write(STDERR_FILENO, crash_banner.data(), crash_banner.size());
        },
        nullptr);
    llvm::sys::PrintStackTraceOnErrorSignal(llvm::StringRef(argv0.data(), argv0.size()));
}

} // namespace beman::specgen::frontend
