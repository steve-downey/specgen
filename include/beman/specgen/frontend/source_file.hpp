// include/beman/specgen/frontend/source_file.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A source file named with `generate --source`, and the options it is parsed
// with. Below the entry points and the harvest that both take one.

#ifndef BEMAN_SPECGEN_FRONTEND_SOURCE_FILE_HPP
#define BEMAN_SPECGEN_FRONTEND_SOURCE_FILE_HPP

#include <beman/specgen/frontend/compile_args.hpp>

#include <string>

namespace beman::specgen::frontend {

// A source file whose out-of-line member definitions carry the markup for
// members the header declares (issue #149), and the options it is parsed with:
// its own compile command's, since it is a translation unit of its own. Named
// with `generate --source`, after the header whose document it belongs to.
struct SourceFile {
    std::string  path;
    ParseOptions options;
};

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_SOURCE_FILE_HPP
