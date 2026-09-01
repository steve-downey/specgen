// tools/specgen/main.cpp                                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The specgen driver.
//
// Two subcommands, deliberately split along the build tiers:
//
//   render      IR JSON in, wording fragments out. Needs no compiler, so it is
//               available in every build and is what the backend goldens run
//               through on every CI lane.
//   generate    header in, wording out. Needs the Clang front end, and
//               reports so plainly when the build did not include it.
//   dump-decls  debug mode: prints the decl/comment interleave design
//               §3.2 builds for a header. Needs the Clang front end too.

#include <beman/specgen/backend/latex.hpp>
#include <beman/specgen/backend/mpark.hpp>
#include <beman/specgen/backend/org.hpp>
#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/foundation/fold_left_short.hpp>
#include <beman/specgen/fragments.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/validate/validate.hpp>

#include <beman/specgen/frontend/frontend.hpp>

#include <algorithm>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fragments = beman::specgen::fragments;
namespace ir        = beman::specgen::ir;
namespace latex     = beman::specgen::backend::latex;
namespace mpark     = beman::specgen::backend::mpark;
namespace org       = beman::specgen::backend::org;
namespace validate  = beman::specgen::validate;

constexpr std::string_view kVersion = "0.1.0";

// The usage text is passed to std::print as an *argument*, never as the
// format string: it is ordinary prose that happens to be a string literal,
// and formatting it would make any brace someone later adds to it a parse
// error rather than a character (decision format-print-output).
constexpr std::string_view kUsage = R"(usage: specgen <command> [options]

commands:
  render      render IR JSON to wording fragments
  generate    render a C++ header to wording fragments
  dump-decls  print a header's decl/comment interleave (debug)

dump-decls options:
  <header>                  the header to parse
  --compile-commands <dir>  read compile flags for <header> from <dir>'s
                             compile_commands.json
  --no-compile-commands     suppress the search for a compile_commands.json
                             above <header>, which is otherwise done by
                             default (no effect alongside --compile-commands
                             or a `--` tail; both already outrank the search)
  -- <clang args>...        pass these to Clang verbatim instead of consulting
                             any compile_commands.json; everything after the
                             first bare `--` is taken as-is, with no further
                             option parsing

generate options:
  <header>                  the header to parse
  --emit-ir                 build the document-tree skeleton (design §3.2)
                             and emit it as IR JSON, instead of the default
                             smoke-check message
  -o, --output <file>       write here instead of standard output
                             (--emit-ir only)
  --compile-commands <dir>  read compile flags for <header> from <dir>'s
                             compile_commands.json
  --no-compile-commands     suppress the search for a compile_commands.json
                             above <header>, which is otherwise done by
                             default (no effect alongside --compile-commands
                             or a `--` tail; both already outrank the search)
  -- <clang args>...        pass these to Clang verbatim instead of consulting
                             any compile_commands.json; everything after the
                             first bare `--` is taken as-is, with no further
                             option parsing

render options:
  --from-ir <file>    IR JSON to read; "-" for standard input (required)
  --backend <name>    latex (default), mpark, or org
  --validate          run the wording validators before rendering; a finding
                       at error severity aborts the render (exit 1) instead
  --paper             wrap the fragment in an `::: add` editing-instruction div
                       and number its paragraphs as added (mpark only)
  --split <dir>       write one fragment per top-level section into <dir>,
                       named from its stable name (optional.ctor.tex), and
                       list the paths written on standard output
  --root <name>       name the fragment holding the nodes outside every
                       section (--split only); derived from the sections'
                       common stable-name prefix when omitted
  -o, --output <file> write here instead of standard output

general:
  -h, --help          show this message
  --version           show the version
)";

int usage(std::FILE* out, int code) {
    std::print(out, "{}", kUsage);
    return code;
}

// Slurping stays on streams (decision format-print-output): std::format and std::print are for
// *formatting*, and this reads bytes rather than interpolating any. This is
// the only reason <iostream>/<sstream> are still included above.
std::optional<std::string> read_all(const std::string& path) {
    if (path == "-") {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        return buffer.str();
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// read_all(), lifted into the expected carrier (decision expected-error-taxonomy: a single fallible step)
// so render_command's pipeline below is one `.and_then(...)` chain instead of
// an `if (!x) return` ladder.
std::expected<std::string, std::string> read_all_or_fail(const std::string& path) {
    if (auto text = read_all(path))
        return *std::move(text);
    return std::unexpected(std::format("specgen: cannot read '{}'", path));
}

// `render`'s options, as the accumulator of the fold below. `awaiting` is the
// spelling of the option whose value the *next* argument supplies — what an
// index loop would express by stepping `i` past it — kept as the
// spelling rather than an enum so the "requires an argument" message can name
// the option the user actually typed.
struct RenderOptions {
    std::string input;
    std::string backend = "latex";
    std::string output;
    std::string split_dir; // render --split <dir>
    std::string root;      // render --root <name>, with --split
    std::string awaiting;
    bool        validate = false; // render --validate
    bool        paper    = false; // render --paper (mpark only)
};

// Why an option scan stopped early. Every stop in `render` is an error; the
// flag only chooses whether the usage text follows the message.
struct OptionError {
    std::string message;
    bool        show_usage = false;
};

// One step of the scan (decision expected-error-taxonomy: the fold's effectful step function).
std::expected<RenderOptions, OptionError> render_option(RenderOptions opts, const std::string& arg) {
    if (!opts.awaiting.empty()) {
        std::string& dest = opts.awaiting == "--from-ir"   ? opts.input
                            : opts.awaiting == "--backend" ? opts.backend
                            : opts.awaiting == "--split"   ? opts.split_dir
                            : opts.awaiting == "--root"    ? opts.root
                                                           : opts.output;
        dest              = arg;
        opts.awaiting.clear();
        return opts;
    }
    if (arg == "--validate") {
        opts.validate = true;
        return opts;
    }
    if (arg == "--paper") {
        opts.paper = true;
        return opts;
    }
    if (arg == "--from-ir" || arg == "--backend" || arg == "--split" || arg == "--root" || arg == "-o" ||
        arg == "--output") {
        opts.awaiting = arg;
        return opts;
    }
    return std::unexpected(OptionError{std::format("specgen: unknown option '{}'", arg), true});
}

int render_command(const std::vector<std::string>& args) {
    // -h/--help is an early *success* exit, so it is answered before the fold
    // rather than inside it. `render_option`'s OptionError deliberately
    // expresses only errors -- "every stop in `render` is an error", as its
    // comment says -- so a help request routed through the scan would come
    // out as `unknown option '--help'` on stderr with exit 2, while the
    // top-level and `generate` spellings print to stdout and exit 0; the
    // examples document advertises all three.
    if (std::ranges::any_of(args, [](const std::string& a) { return a == "-h" || a == "--help"; })) {
        return usage(stdout, 0);
    }

    // The early-stop verb (decision expected-error-taxonomy): an option scan
    // is a sequence that must stop at the first bad argument and look at no
    // later one, which is exactly fold_left_short — not a loop with a
    // `return` buried in its middle.
    const auto options = beman::specgen::foundation::fold_left_short(args, RenderOptions{}, render_option);
    if (!options) {
        std::println(stderr, "{}", options.error().message);
        return options.error().show_usage ? usage(stderr, 2) : 2;
    }
    // An option still awaiting its value when the arguments ran out. Only
    // the final argument can leave the scan in this state, so it is checked
    // once after the fold rather than at every step inside it.
    if (!options->awaiting.empty()) {
        std::println(stderr, "specgen: {} requires an argument", options->awaiting);
        return 2;
    }

    const std::string& input     = options->input;
    const std::string& backend   = options->backend;
    const std::string& output    = options->output;
    const std::string& split_dir = options->split_dir;

    if (input.empty()) {
        std::println(stderr, "specgen: render requires --from-ir");
        return usage(stderr, 2);
    }
    if (backend != "latex" && backend != "mpark" && backend != "org") {
        // Every backend design §8 names exists, so this is a plain
        // misspelling.
        std::println(stderr, "specgen: unknown backend '{}'; 'latex', 'mpark' and 'org' are available", backend);
        return 2;
    }
    // `::: add` is an mpark construct. Silently ignoring the flag on the
    // LaTeX backend would let a paper author believe a fragment was marked as
    // added when it was not, which is the one mistake this mode exists to
    // prevent making by hand.
    if (options->paper && backend != "mpark") {
        std::println(stderr, "specgen: --paper applies only to the mpark backend, not '{}'", backend);
        return 2;
    }
    // --split writes a *set* of files whose names it derives, so there
    // is nothing for a single output path to mean beside it; and --root names
    // one of those files, so it means nothing without them. Both are reported
    // rather than ignored, for the reason --paper is: a flag that quietly does
    // nothing is a fragment set the author believes was written differently.
    if (!split_dir.empty() && !output.empty()) {
        std::println(stderr, "specgen: --split writes a directory of fragments, so --output does not apply");
        return 2;
    }
    if (!options->root.empty() && split_dir.empty()) {
        std::println(stderr, "specgen: --root names a fragment, so it applies only with --split");
        return 2;
    }

    // Driver orchestration (decision expected-error-taxonomy) is an and_then
    // pipeline over stages, not an `if (!x) return` ladder — read the file,
    // parse it, render it, write it,
    // each step short-circuiting the rest on failure. All four stages share
    // one error type (a ready-to-print message) since nothing downstream of
    // main() needs to distinguish which stage failed.
    // Both branches write an already-built document, so this is bulk I/O, not
    // formatting: `{}` on stdout and `<<` on the file stream, rather than
    // running a whole rendered fragment back through the format machinery
    // (decision format-print-output's boundary rule).
    auto write_output = [&](std::string rendered) -> std::expected<int, std::string> {
        if (output.empty()) {
            std::print(stdout, "{}", rendered);
            return 0;
        }
        std::ofstream out(output, std::ios::binary);
        if (!out)
            return std::unexpected(std::format("specgen: cannot write '{}'", output));
        out << rendered;
        return out ? 0 : 1;
    };

    // The backend seam (decision ir-boundary): one IR, one call, chosen by
    // name. Validated above, so there is no fourth case. A lambda rather than
    // the tail of the pipeline because the split path calls it once per fragment.
    auto render_document = [&](const ir::Document& document) {
        if (backend == "mpark")
            return mpark::render_to_string(document, {.paper_mode = options->paper});
        if (backend == "org")
            return org::render_to_string(document);
        return latex::render_to_string(document);
    };

    // The extension is the only part of design §8's fragment-path scheme
    // that is a fact about the backend. The stem is the stable name, and
    // deriving it is Tier A's (fragments.hpp) — the same split serves all
    // three targets, which is why nothing about it lives in a backend.
    const std::string_view extension = backend == "mpark" ? ".md" : backend == "org" ? ".org" : ".tex";

    auto write_fragments = [&](const ir::Document& document) -> std::expected<int, std::string> {
        // Validation, if it ran at all, ran over the whole document above:
        // the roster and the two document-level channels it reads describe the
        // header, not any one section of it, so splitting first would ask
        // every rule a narrower question than the one design §9 poses.
        const std::expected<std::vector<fragments::Fragment>, fragments::Error> pieces =
            fragments::split(document, {.root = options->root});
        if (!pieces)
            return std::unexpected(std::format(
                "specgen: {}{}", pieces.error().message, pieces.error().root_unnamed ? "; name it with --root" : ""));

        std::error_code failed;
        std::filesystem::create_directories(split_dir, failed);
        if (failed)
            return std::unexpected(std::format("specgen: cannot create '{}': {}", split_dir, failed.message()));

        // The early-stop verb again (decision expected-error-taxonomy):
        // writing a set of files is a sequence that must stop at the first
        // failure, and what it accumulates is the manifest — the paths in
        // document order, which is the one thing the directory listing
        // loses. It is printed once at the end rather than a line at a time
        // (decision format-print-output), so a failed write leaves no
        // half-list claiming files that are not there.
        const std::expected<std::string, std::string> manifest = beman::specgen::foundation::fold_left_short(
            *pieces,
            std::string{},
            [&](std::string listing, const fragments::Fragment& fragment) -> std::expected<std::string, std::string> {
                const std::filesystem::path path =
                    std::filesystem::path(split_dir) / std::format("{}{}", fragment.name, extension);
                std::ofstream out(path, std::ios::binary);
                if (!out)
                    return std::unexpected(std::format("specgen: cannot write '{}'", path.string()));
                out << render_document(fragment.document);
                if (!out)
                    return std::unexpected(std::format("specgen: cannot write '{}'", path.string()));
                return std::format("{}{}\n", listing, path.generic_string());
            });
        if (!manifest)
            return std::unexpected(manifest.error());

        std::print(stdout, "{}", *manifest);
        return 0;
    };

    auto result =
        read_all_or_fail(input)
            .and_then([&](const std::string& text) {
                // ir::parse_document reports a ParseError; the only
                // adaptation this stage needs is to render it in the
                // pipeline's shared message type, which is what
                // transform_error is for.
                return ir::parse_document(text).transform_error([&](const ir::ParseError& error) {
                    return std::format("{}:{}: error: {}", input, error.offset, error.message);
                });
            })
            .and_then([&](ir::Document document) -> std::expected<ir::Document, std::string> {
                // render --validate's one inserted stage,
                // between parse and render. Off by default (the
                // flag defaults false), so this and_then is the
                // identity on every existing golden -- byte
                // identity by construction, not by inspection.
                if (!options->validate)
                    return document;
                const validate::Diagnostics findings = validate::validate(document);
                if (!validate::has_errors(findings)) {
                    // substrate generic algorithm
                    // Formatted output, not a fold: nothing is
                    // accumulated and nothing is returned, so
                    // there is no verb to name here (mirrors the
                    // diagnostic print in generate_command below).
                    for (const auto& finding : findings)
                        std::println(stderr, "specgen: {}", validate::format_diagnostic(finding));
                    return document;
                }
                // Every finding, one per line, sharing the same
                // format_diagnostic text the warning branch above
                // prints -- so warning and error wording cannot
                // drift apart. The `if (!result)` tail below prints
                // this and returns 1; rendering never runs.
                const std::string message =
                    findings | std::views::transform([](const validate::Diagnostic& finding) {
                        return std::format("specgen: {}", validate::format_diagnostic(finding));
                    }) |
                    std::views::join_with('\n') | std::ranges::to<std::string>();
                return std::unexpected(message);
            })
            .and_then([&](const ir::Document& document) -> std::expected<int, std::string> {
                // One document in, one file or a directory of them
                // out. Which it is, is the last thing the
                // pipeline decides, because everything above it —
                // reading, parsing, validating — is the same work
                // either way.
                return split_dir.empty() ? write_output(render_document(document)) : write_fragments(document);
            });

    if (!result) {
        std::println(stderr, "{}", result.error());
        return 1;
    }
    return *result;
}

int generate_command(const std::vector<std::string>& args) {
    // `--emit-ir` runs the real path — parse the
    // header, build the document tree (design §3.2), print it as IR JSON.
    // Without the flag this only proves the default smoke-check path.
    namespace frontend = beman::specgen::frontend;

    // Everything after the first bare "--" is the clang-argument
    // tail, verbatim, with no further option parsing -- split it off before
    // the loop below ever sees it, so an "-I" meant for Clang is never
    // mistaken for one of this command's own options.
    const auto                     dash_dash = std::ranges::find(args, std::string_view("--"));
    const std::vector<std::string> head(args.begin(), dash_dash);
    const std::vector<std::string> tail(dash_dash == args.end() ? args.end() : dash_dash + 1, args.end());

    std::string header;
    std::string output;
    std::string compile_commands_dir;
    bool        emit_ir             = false;
    bool        no_compile_commands = false;

    // substrate generic algorithm
    // Two facts keep this a loop, not a fold_left_short: the index is
    // load-bearing -- next() advances i from inside the body to consume an
    // option's argument, which is the loop counter itself moving, not a read
    // at another position (CODING_RULES' zip tell) -- and --help is an early
    // *success* exit, which no short-circuit-on-error fold expresses without
    // blending two of the error taxonomy's four rows at one call site
    // (decision expected-error-taxonomy).
    for (std::size_t i = 0; i < head.size(); ++i) {
        const std::string& arg  = head[i];
        auto               next = [&](std::string& dest) {
            if (i + 1 >= head.size()) {
                std::println(stderr, "specgen: {} requires an argument", arg);
                return false;
            }
            dest = head[++i];
            return true;
        };
        if (arg == "-h" || arg == "--help") {
            return usage(stdout, 0);
        } else if (arg == "--emit-ir") {
            emit_ir = true;
        } else if (arg == "-o" || arg == "--output") {
            if (!next(output))
                return 2;
        } else if (arg == "--compile-commands") {
            if (!next(compile_commands_dir))
                return 2;
        } else if (arg == "--no-compile-commands") {
            no_compile_commands = true;
        } else if (!arg.empty() && arg.front() != '-') {
            header = arg;
        } else {
            std::println(stderr, "specgen: unknown option '{}'", arg);
            return usage(stderr, 2);
        }
    }

    if (emit_ir) {
        if (header.empty()) {
            std::println(stderr, "specgen: generate --emit-ir requires a header");
            return usage(stderr, 2);
        }

        // Probing defaults on for the driver -- it is what makes
        // the acid target (which has a root compile_commands.json) parseable
        // at all -- with --no-compile-commands as the escape hatch; a `--`
        // tail or an explicit --compile-commands both take precedence over
        // probing regardless (ParseOptions' own precedence order), so setting
        // this unconditionally is safe. Resolved once here, rather than left
        // to build_document below, so the "which file they came from"
        // diagnostic and the actual parse read the identical answer: passing
        // the resolved args back in as `extra_args` (top precedence) means
        // build_document performs no second filesystem probe.
        const frontend::ParseOptions raw_options{.extra_args             = tail,
                                                 .compile_commands_dir   = compile_commands_dir,
                                                 .probe_compile_commands = !no_compile_commands};
        const frontend::ResolvedArgs resolved = frontend::resolve_extra_args(header, raw_options);
        if (!resolved.source.empty())
            std::println(stderr,
                         "specgen: using compile flags for '{}' from the database entry in '{}'",
                         header,
                         resolved.source);
        const frontend::ParseOptions options{
            .extra_args = resolved.args, .compile_commands_dir = {}, .probe_compile_commands = false};

        // Build, then emit, as one and_then pipeline (decision
        // expected-error-taxonomy) — both stages share
        // frontend::BuildFailure as their error type, so a write failure
        // reports the same way a build failure does. Diagnostics collected
        // along the way are printed but
        // never turn a successful build into a failed one.
        auto result =
            frontend::build_document(header, options)
                .and_then([&](beman::specgen::document_build::BuildResult built)
                              -> std::expected<int, frontend::BuildFailure> {
                    // substrate generic algorithm
                    // Formatted output, not a fold: nothing is accumulated and
                    // nothing is returned, so there is no verb to name here.
                    //
                    // `line: severity:`: the location is a line number the
                    // reader can actually find, not a byte offset, and the
                    // severity is each finding's own — notes and errors are
                    // not all printed as the literal word "warning". An Error
                    // here still does not fail the build: failing it would
                    // withhold the emitted IR, not just the finding's wording.
                    for (const auto& diagnostic : built.diagnostics)
                        std::println(stderr,
                                     "{}:{}: {}: {}",
                                     header,
                                     diagnostic.line,
                                     beman::specgen::severity_label(diagnostic.severity),
                                     diagnostic.message);

                    if (output.empty()) {
                        std::println(stdout, "{}", ir::emit_json(built.document));
                        return 0;
                    }
                    std::ofstream out(output, std::ios::binary);
                    if (!out)
                        return std::unexpected(frontend::BuildFailure{std::format("cannot write '{}'", output)});
                    out << ir::emit_json(built.document) << '\n';
                    return out ? 0 : 1;
                });

        if (!result) {
            std::println(stderr, "specgen: {}", result.error().message);
            return 1;
        }
        return *result;
    }

    // The default smoke path: a stock snippet unless a header was named, in
    // which case reading it is one fallible step (decision
    // expected-error-taxonomy) rather than an
    // `if (!text)` ladder around a std::optional.
    const std::expected<std::string, std::string> source =
        header.empty() ? std::expected<std::string, std::string>{"int f(int x);"} : read_all_or_fail(header);
    if (!source) {
        std::println(stderr, "{}", source.error());
        return 1;
    }

    const frontend::SmokeResult result = frontend::smoke_check(*source);
    if (!result.ast_built) {
        std::println(stderr, "specgen: the Clang front end could not parse the input");
        return 1;
    }
    std::println(stderr,
                 "specgen: Clang front-end tier OK; pass --emit-ir to generate "
                 "wording IR");
    return 0;
}
// Debug output is one line per item, so a multi-line raw comment
// (a docblock, or several merged `//` lines) is collapsed to its first line;
// the full text is still what frontend::SourceItem::label carries.
std::string first_line(const std::string& text) {
    const auto newline = text.find('\n');
    return newline == std::string::npos ? text : text.substr(0, newline);
}

int dump_decls_command(const std::vector<std::string>& args) {
    namespace frontend = beman::specgen::frontend;

    // Same "--" tail split as generate_command.
    const auto                     dash_dash = std::ranges::find(args, std::string_view("--"));
    const std::vector<std::string> head(args.begin(), dash_dash);
    const std::vector<std::string> tail(dash_dash == args.end() ? args.end() : dash_dash + 1, args.end());

    std::string header;
    std::string compile_commands_dir;
    bool        no_compile_commands = false;

    // substrate generic algorithm: same index-load-bearing shape as
    // generate_command's identical loop above, and for the same reason --
    // next() advances i from inside the body.
    for (std::size_t i = 0; i < head.size(); ++i) {
        const std::string& arg  = head[i];
        auto               next = [&](std::string& dest) {
            if (i + 1 >= head.size()) {
                std::println(stderr, "specgen: {} requires an argument", arg);
                return false;
            }
            dest = head[++i];
            return true;
        };
        if (arg == "-h" || arg == "--help") {
            // An early *success* exit, the same as generate_command's:
            // `dump-decls --help` -- a command the examples document
            // advertises -- prints usage to stdout and exits 0 like its two
            // sibling commands, rather than falling into the "requires a
            // header path" error below.
            // The empty-argument case is separate and unaffected:
            // header stays empty and the check below reports it verbatim.
            return usage(stdout, 0);
        } else if (arg == "--compile-commands") {
            if (!next(compile_commands_dir))
                return 2;
        } else if (arg == "--no-compile-commands") {
            no_compile_commands = true;
        } else if (!arg.empty() && arg.front() != '-') {
            header = arg;
        } else {
            std::println(stderr, "specgen: unknown option '{}'", arg);
            return usage(stderr, 2);
        }
    }

    if (header.empty()) {
        std::println(stderr, "specgen: dump-decls requires a header path");
        return usage(stderr, 2);
    }

    // Same resolve-once-and-reuse shape as generate_command's --emit-ir path.
    const frontend::ParseOptions raw_options{.extra_args             = tail,
                                             .compile_commands_dir   = compile_commands_dir,
                                             .probe_compile_commands = !no_compile_commands};
    const frontend::ResolvedArgs resolved = frontend::resolve_extra_args(header, raw_options);
    if (!resolved.source.empty())
        std::println(
            stderr, "specgen: using compile flags for '{}' from the database entry in '{}'", header, resolved.source);
    const frontend::ParseOptions options{
        .extra_args = resolved.args, .compile_commands_dir = {}, .probe_compile_commands = false};

    const frontend::InterleaveResult interleaved = frontend::collect_interleaved(header, options);
    // dump-decls keeps running on a partial parse rather than failing
    // on it -- generate does the opposite (see build_document's BuildFailure)
    // because this command's whole point is a debugging view of exactly what
    // the parse produced, and a partial collection is that view when the
    // parse is partial, not a reason to withhold it.
    if (interleaved.had_parse_error)
        std::println(stderr,
                     "specgen: warning: Clang reported an error parsing '{}'; the interleave below may be "
                     "incomplete or wrong",
                     header);

    // substrate generic algorithm
    // Formatted output, not a fold: nothing is accumulated and nothing is
    // returned, so there is no verb to name here (same shape as render's
    // warning printer and generate's diagnostic printer above).
    for (const frontend::SourceItem& item : interleaved.items) {
        std::println(stdout,
                     "[{}] {}{}",
                     item.offset,
                     item.kind == frontend::SourceItem::Kind::Declaration ? "DECL " : "COMMENT ",
                     first_line(item.label));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
        return usage(stderr, 2);

    const std::string& command = args.front();
    if (command == "-h" || command == "--help")
        return usage(stdout, 0);
    if (command == "--version") {
        std::println(stdout, "specgen {}", kVersion);
        return 0;
    }
    if (command == "render")
        return render_command({args.begin() + 1, args.end()});
    if (command == "generate")
        return generate_command({args.begin() + 1, args.end()});
    if (command == "dump-decls")
        return dump_decls_command({args.begin() + 1, args.end()});

    std::println(stderr, "specgen: unknown command '{}'", command);
    return usage(stderr, 2);
}
