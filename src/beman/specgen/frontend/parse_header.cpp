// src/beman/specgen/frontend/parse_header.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/parse_header.hpp>

#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>

#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Options/OptionUtils.h>
#include <clang/Tooling/Tooling.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

namespace {

// The `-resource-dir` that points Clang at the builtin headers (<stddef.h>,
// <stdarg.h>, ...) specgen carries beside itself, or nothing when that
// directory is not there.
//
// Left to itself, buildASTFromCodeWithArgs names the compiler `clang-tool`,
// with no path in it, and derives the resource directory from that name: the
// *relative* `lib/clang/<major>`, resolved against whatever directory the user
// happens to be in. A distribution's libclang-cpp may bake in a fallback
// search directory that hides this, but a machine without the pinned LLVM has
// no such fallback, and every header that reaches the standard library reaches
// <stddef.h>. So ask Clang's own rule, `<executable dir>/../lib/clang/<major>`,
// with the executable this process really is; the build tree and the install
// both lay that directory out (tools/specgen/CMakeLists.txt). A driver that
// finds nothing there -- a unit-test binary, which has no such sibling -- keeps
// Clang's default rather than being handed a directory that does not exist.
//
// It goes ahead of the resolved arguments so that a `-resource-dir` from the
// `--` tail or a compile command overrides it by the same last-wins parsing
// the `-std` default relies on.
std::vector<std::string> bundled_resource_dir_args() {
    static const std::vector<std::string> args = [] {
        // Any address inside this executable, for the platforms where the
        // executable is found by asking which image holds it.
        static const int  anchor = 0;
        const std::string dir    = clang::GetResourcesPath("specgen", const_cast<int*>(&anchor));
        if (std::error_code ec; !std::filesystem::is_directory(dir, ec))
            return std::vector<std::string>{};
        return std::vector<std::string>{"-resource-dir", dir};
    }();
    return args;
}

} // namespace

ParsedHeader parse_header(std::string_view header_path, const ParseOptions& options) {
    std::ifstream in(std::string(header_path), std::ios::binary);
    if (!in)
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();

    // specgen's own -std default first (a project's own -std, if it
    // supplies one, overrides it — clang's last-wins argument parsing makes
    // this ordering meaningful), the resolved args in the middle, and the two
    // structural requirements last, where that same last-wins parsing
    // protects them from being overridden by anything a caller supplies.
    std::vector<std::string> args = {"-std=c++2c"};
    args.append_range(bundled_resource_dir_args());
    args.append_range(resolve_extra_args(header_path, options).args);
    args.push_back("-fsyntax-only");
    args.push_back("-fparse-all-comments");
    args.push_back("-Xclang");
    args.push_back("-detailed-preprocessing-record");
    std::unique_ptr<clang::ASTUnit> ast =
        clang::tooling::buildASTFromCodeWithArgs(buffer.str(), args, std::string(header_path));
    const bool had_error = ast && ast->getDiagnostics().hasErrorOccurred();
    return ParsedHeader{std::move(ast), had_error};
}

std::vector<std::string> parse_sources(std::string_view header_path, const clang::ASTUnit& ast) {
    const clang::SourceManager& sm = ast.getSourceManager();

    // Lexically, not `weakly_canonical`: `./a.hpp` and `a.hpp` are one file and
    // have to dedup as one -- a header named on the command line is routinely
    // `#include`d by a sibling too, and the two spellings reached the
    // SourceManager by different routes. Resolving any further would make the
    // paths absolute and follow symlinks, and a dependency fragment is read
    // from the directory the build runs in, where the relative spelling is the
    // one that works.
    const auto normalized = [](std::string_view path) {
        return std::filesystem::path(path).lexically_normal().generic_string();
    };

    std::vector<std::string> sources;
    // substrate generic algorithm: a filter-map over a DenseMap's own iterator
    // pair, which is not a range and so cannot be piped through views.
    for (clang::SourceManager::fileinfo_iterator it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
        const clang::FileID file = sm.translateFile(it->first);
        if (file.isValid() && sm.isInSystemHeader(sm.getLocForStartOfFile(file)))
            continue;
        sources.push_back(normalized(it->first.getName()));
    }

    // A DenseMap iterates in hash order, so sorting is not presentation here:
    // an unsorted list would make two runs over one unchanged header produce
    // two different dependency fragments.
    std::ranges::sort(sources);
    const auto duplicates = std::ranges::unique(sources);
    sources.erase(duplicates.begin(), duplicates.end());
    // The main file leads, and appears once however it was reached.
    const std::string main_file = normalized(header_path);
    std::erase(sources, main_file);
    sources.insert(sources.begin(), main_file);
    return sources;
}

BuildFailure unparsed_header(std::string_view header_path) {
    return BuildFailure{std::format("cannot read '{}', or Clang could not parse it", header_path)};
}

} // namespace beman::specgen::frontend
