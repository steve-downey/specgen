// tools/split_inline/main.cpp                                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// split_inline: move a header's in-class member function definitions out of
// line (docs/plans/split-inline.md). Parse the header the way `specgen
// generate` would, plan the rewrite, check that the result still parses, and
// write it.

#include <beman/specgen/split_inline/rewrite.hpp>

#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/parse_header.hpp>

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace frontend     = beman::specgen::frontend;
namespace split_inline = beman::specgen::split_inline;

constexpr std::string_view kVersion   = BEMAN_SPECGEN_VERSION;
constexpr std::string_view kGitCommit = BEMAN_SPECGEN_GIT_COMMIT;

// Passed to std::print as an argument, never as the format string (decision
// format-print-output).
constexpr std::string_view kUsage = R"(usage: split_inline <header> [options] [-- <clang args>...]

Move the in-class member function definitions of every class in <header> out
of line, after the class and inside the namespace block that encloses it.
Hidden friends, defaulted and deleted members stay in the class.

options:
  -o <file>                 write the converted header to <file>; `-` (the
                             default) is standard output
  --in-place                write the converted header over <header>
  --move-comments           carry the `//!` docblock above a moved definition
                             along with it, and place the definition under the
                             `\rSec` section its `\ref` group names, writing
                             the section header when the header has none yet;
                             without this every comment stays where it is
  --section-depth <n>       the `\rSec` depth of a header the tool writes when
                             no sibling section is there to copy it from
                             (default 3)
  --unchecked               write the result even if it no longer parses
  --compile-commands <dir>  read compile flags for <header> from <dir>'s
                             compile_commands.json
  --no-compile-commands     suppress the search for a compile_commands.json
                             above <header>, which is otherwise done by default
  -- <clang args>...        pass these to Clang verbatim instead of consulting
                             any compile_commands.json
  --version                 print the version and exit
  -h, --help                print this text and exit

A member the tool will not move is reported on standard error and left in the
class; the rest of the header is still converted. Exit status is 0 when every
in-class definition moved, 1 when any was refused or the header did not parse,
2 for a usage error.
)";

int usage(std::FILE* to, int status) {
    std::print(to, "{}", kUsage);
    return status;
}

struct Arguments {
    std::string              header;
    std::string              output    = "-";
    bool                     in_place  = false;
    bool                     unchecked = false;
    split_inline::Options    options;
    std::string              compile_commands_dir;
    bool                     no_compile_commands = false;
    std::vector<std::string> clang_args;
};

std::optional<std::string> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_all(const std::string& path, std::string_view text) {
    if (path == "-") {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return std::fflush(stdout) == 0;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// The parse `generate` would do of the same header: the same precedence of
// compile-argument sources, resolved once.
frontend::ParseOptions parse_options(const Arguments& args) {
    const frontend::ParseOptions raw{.extra_args             = args.clang_args,
                                     .compile_commands_dir   = args.compile_commands_dir,
                                     .probe_compile_commands = !args.no_compile_commands};
    const frontend::ResolvedArgs resolved = frontend::resolve_extra_args(args.header, raw);
    if (!resolved.source.empty())
        std::println(stderr,
                     "split_inline: using compile flags for '{}' from the database entry in '{}'",
                     args.header,
                     resolved.source);
    return frontend::ParseOptions{
        .extra_args = resolved.args, .compile_commands_dir = {}, .probe_compile_commands = false};
}

// Whether the converted text parses without errors under the same options,
// checked from a temporary file beside the header so relative includes
// resolve as they did.
bool reparses_clean(const Arguments& args, const frontend::ParseOptions& options, std::string_view text) {
    llvm::SmallString<256> model(llvm::sys::path::parent_path(args.header));
    llvm::sys::path::append(model, ".split_inline-%%%%%%.hpp");
    llvm::SmallString<256> temp;
    int                    fd = -1;
    if (llvm::sys::fs::createUniqueFile(model, fd, temp)) {
        std::println(stderr, "split_inline: could not create a temporary file beside '{}'", args.header);
        return false;
    }
    {
        llvm::raw_fd_ostream out(fd, /*shouldClose=*/true);
        out << text;
    }
    const frontend::ParsedHeader check = frontend::parse_header(std::string_view(temp), options);
    llvm::sys::fs::remove(temp);
    return check.ast != nullptr && !check.had_error;
}

std::optional<Arguments> parse_arguments(const std::vector<std::string>& argv, int& status) {
    Arguments  args;
    const auto dash_dash = std::ranges::find(argv, std::string_view("--"));
    args.clang_args.assign(dash_dash == argv.end() ? argv.end() : std::next(dash_dash), argv.end());
    const std::vector<std::string> head(argv.begin(), dash_dash);

    // substrate generic algorithm: an option scan whose `next` advances the
    // index from inside the body, the shape the driver's commands share.
    for (std::size_t i = 0; i < head.size(); ++i) {
        const std::string& arg  = head[i];
        const auto         next = [&](std::string& dest) {
            if (i + 1 >= head.size()) {
                std::println(stderr, "split_inline: {} requires an argument", arg);
                return false;
            }
            dest = head[++i];
            return true;
        };
        if (arg == "-h" || arg == "--help") {
            status = usage(stdout, 0);
            return std::nullopt;
        } else if (arg == "--version") {
            std::println("split_inline {} (git {})", kVersion, kGitCommit);
            status = 0;
            return std::nullopt;
        } else if (arg == "-o") {
            if (!next(args.output)) {
                status = 2;
                return std::nullopt;
            }
        } else if (arg == "--in-place") {
            args.in_place = true;
        } else if (arg == "--move-comments") {
            args.options.comments = split_inline::Comments::Move;
        } else if (arg == "--unchecked") {
            args.unchecked = true;
        } else if (arg == "--section-depth") {
            std::string depth;
            if (!next(depth)) {
                status = 2;
                return std::nullopt;
            }
            const auto [end, ec] =
                std::from_chars(depth.data(), depth.data() + depth.size(), args.options.section_depth);
            if (ec != std::errc{} || end != depth.data() + depth.size() || args.options.section_depth < 0) {
                std::println(
                    stderr, "split_inline: --section-depth wants a small non-negative integer, not '{}'", depth);
                status = 2;
                return std::nullopt;
            }
        } else if (arg == "--compile-commands") {
            if (!next(args.compile_commands_dir)) {
                status = 2;
                return std::nullopt;
            }
        } else if (arg == "--no-compile-commands") {
            args.no_compile_commands = true;
        } else if (!arg.empty() && arg.front() != '-') {
            if (!args.header.empty()) {
                std::println(stderr,
                             "split_inline: converts one header per run; '{}' and '{}' were both given",
                             args.header,
                             arg);
                status = 2;
                return std::nullopt;
            }
            args.header = arg;
        } else {
            std::println(stderr, "split_inline: unknown option '{}'", arg);
            status = usage(stderr, 2);
            return std::nullopt;
        }
    }
    if (args.header.empty()) {
        std::println(stderr, "split_inline: a header path is required");
        status = usage(stderr, 2);
        return std::nullopt;
    }
    if (args.in_place && args.output != "-") {
        std::println(stderr, "split_inline: --in-place and -o name two destinations; give one");
        status = 2;
        return std::nullopt;
    }
    if (args.in_place)
        args.output = args.header;
    return args;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> argv_strings(argv + 1, argv + argc);
    int                            status = 0;
    const std::optional<Arguments> parsed = parse_arguments(argv_strings, status);
    if (!parsed)
        return status;
    const Arguments& args = *parsed;

    const std::optional<std::string> source = read_all(args.header);
    if (!source) {
        std::println(stderr, "split_inline: cannot read '{}'", args.header);
        return 1;
    }
    const frontend::ParseOptions options       = parse_options(args);
    frontend::ParsedHeader       parsed_header = frontend::parse_header(args.header, options);
    if (parsed_header.ast == nullptr) {
        std::println(stderr, "split_inline: Clang could not parse '{}'", args.header);
        return 1;
    }
    if (parsed_header.had_error) {
        std::println(stderr,
                     "split_inline: Clang reported errors parsing '{}'; a rewrite of a partial AST is not safe, so "
                     "nothing was written. Fix the header, or pass the compile flags it needs after `--`",
                     args.header);
        return 1;
    }

    const split_inline::Rewrite plan   = split_inline::plan_rewrite(*parsed_header.ast, args.options);
    const std::string           result = split_inline::apply_edits(*source, plan.edits);
    std::ranges::for_each(plan.refusals, [&](const split_inline::Refusal& r) {
        std::println(stderr, "split_inline: {}:{}: refused: {}", args.header, r.line, r.reason);
    });

    if (!args.unchecked && plan.moved > 0 && !reparses_clean(args, options, result)) {
        std::println(stderr,
                     "split_inline: the converted '{}' does not parse cleanly, so nothing was written; "
                     "--unchecked writes it anyway for inspection",
                     args.header);
        return 1;
    }
    if (!write_all(args.output, result)) {
        std::println(stderr, "split_inline: cannot write '{}'", args.output);
        return 1;
    }
    return plan.refusals.empty() ? 0 : 1;
}
