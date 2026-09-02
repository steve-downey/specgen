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
//   generate    header in, wording out, in one pass: the IR stays in memory
//               and is never written anywhere unless `--emit-ir` asks for it,
//               in which case emitting it is all the command does. Needs the
//               Clang front end, and reports so plainly when the build did
//               not include it.
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
  <header>                  the header to parse; with none, parse a stock
                             snippet as a link-proving smoke check
  --emit-ir                 emit the document tree (design §3.2) as IR JSON
                             for a later `render --from-ir`, instead of
                             rendering wording here
  --backend <name>          latex (default), mpark, or org
  --validate                run the wording validators before rendering; a
                             finding at error severity aborts the render
                             (exit 1) instead
  --paper                   wrap the fragment in an `::: add` editing-instruction
                             div and number its paragraphs as added (mpark only)
  --split <dir>             write one fragment per top-level section into <dir>,
                             named from its stable name (optional.ctor.tex), and
                             list the paths written on standard output
  --root <name>             name the fragment holding the nodes outside every
                             section (--split only); derived from the sections'
                             common stable-name prefix when omitted
  -o, --output <file>       write here instead of standard output
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

// Everything the driver needs to turn one ir::Document into wording. The IR is
// the boundary between the front end and the backends (decision ir-boundary),
// and a boundary is not a file format: serializing it is what `--emit-ir` is
// for, not something the pipeline needs. So `render`, which reads the document
// from IR JSON, and `generate`, which builds it from a header in this same
// process, share this half of the work — and these options — verbatim.
struct WordingOptions {
    std::string backend = "latex";
    std::string output;
    std::string split_dir;        // --split <dir>
    std::string root;             // --root <name>, with --split
    bool        validate = false; // --validate
    bool        paper    = false; // --paper (mpark only)
};

// `render`'s options, as the accumulator of the fold below. `awaiting` is the
// spelling of the option whose value the *next* argument supplies — what an
// index loop would express by stepping `i` past it — kept as the
// spelling rather than an enum so the "requires an argument" message can name
// the option the user actually typed.
struct RenderOptions {
    std::string    input;
    WordingOptions wording;
    std::string    awaiting;
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
                            : opts.awaiting == "--backend" ? opts.wording.backend
                            : opts.awaiting == "--split"   ? opts.wording.split_dir
                            : opts.awaiting == "--root"    ? opts.wording.root
                                                           : opts.wording.output;
        dest              = arg;
        opts.awaiting.clear();
        return opts;
    }
    if (arg == "--validate") {
        opts.wording.validate = true;
        return opts;
    }
    if (arg == "--paper") {
        opts.wording.paper = true;
        return opts;
    }
    if (arg == "--from-ir" || arg == "--backend" || arg == "--split" || arg == "--root" || arg == "-o" ||
        arg == "--output") {
        opts.awaiting = arg;
        return opts;
    }
    return std::unexpected(OptionError{std::format("specgen: unknown option '{}'", arg), true});
}

// The option combinations that are reported rather than quietly ignored. A
// flag that does nothing is wording the author believes was produced
// differently, which is the mistake these modes exist to prevent making by
// hand. The result is a ready-to-print message; every one of them is a usage
// error, so the caller exits 2.
std::optional<std::string> wording_option_error(const WordingOptions& options) {
    // Every backend design §8 names exists, so this is a plain misspelling.
    if (options.backend != "latex" && options.backend != "mpark" && options.backend != "org")
        return std::format("specgen: unknown backend '{}'; 'latex', 'mpark' and 'org' are available", options.backend);
    // `::: add` is an mpark construct. Silently ignoring the flag on the LaTeX
    // backend would let a paper author believe a fragment was marked as added
    // when it was not.
    if (options.paper && options.backend != "mpark")
        return std::format("specgen: --paper applies only to the mpark backend, not '{}'", options.backend);
    // --split writes a *set* of files whose names it derives, so there is
    // nothing for a single output path to mean beside it; and --root names one
    // of those files, so it means nothing without them.
    if (!options.split_dir.empty() && !options.output.empty())
        return std::string("specgen: --split writes a directory of fragments, so --output does not apply");
    if (!options.root.empty() && options.split_dir.empty())
        return std::string("specgen: --root names a fragment, so it applies only with --split");
    return std::nullopt;
}

// `--validate`'s one inserted stage, between the document and the render. Off
// by default, so this is the identity on every existing golden — byte identity
// by construction, not by inspection.
std::expected<void, std::string> validate_document(const ir::Document& document, bool enabled) {
    if (!enabled)
        return {};
    const validate::Diagnostics findings = validate::validate(document);
    if (!validate::has_errors(findings)) {
        // substrate generic algorithm
        // Formatted output, not a fold: nothing is accumulated and nothing is
        // returned, so there is no verb to name here (mirrors the diagnostic
        // print in generate_command below).
        for (const auto& finding : findings)
            std::println(stderr, "specgen: {}", validate::format_diagnostic(finding));
        return {};
    }
    // Every finding, one per line, sharing the same format_diagnostic text the
    // warning branch above prints -- so warning and error wording cannot drift
    // apart. The caller prints this and returns 1; rendering never runs.
    return std::unexpected(findings | std::views::transform([](const validate::Diagnostic& finding) {
                               return std::format("specgen: {}", validate::format_diagnostic(finding));
                           }) |
                           std::views::join_with('\n') | std::ranges::to<std::string>());
}

// The back half of the driver: one document in, one file -- or a directory of
// them -- out. `render` arrives here from IR JSON and `generate` from a header
// it has just parsed, and nothing below can tell which, so the single-pass
// wording is byte-identical to the two-pass route by construction rather than
// by comparison.
std::expected<int, std::string> emit_wording(const ir::Document& document, const WordingOptions& options) {
    // Both branches write an already-built document, so this is bulk I/O, not
    // formatting: `{}` on stdout and `<<` on the file stream, rather than
    // running a whole rendered fragment back through the format machinery
    // (decision format-print-output's boundary rule).
    auto write_output = [&](std::string rendered) -> std::expected<int, std::string> {
        if (options.output.empty()) {
            std::print(stdout, "{}", rendered);
            return 0;
        }
        std::ofstream out(options.output, std::ios::binary);
        if (!out)
            return std::unexpected(std::format("specgen: cannot write '{}'", options.output));
        out << rendered;
        return out ? 0 : 1;
    };

    // The backend seam (decision ir-boundary): one IR, one call, chosen by
    // name. Validated by wording_option_error above, so there is no fourth
    // case. A lambda rather than the tail of the pipeline because the split
    // path calls it once per fragment.
    auto render_document = [&](const ir::Document& fragment) {
        if (options.backend == "mpark")
            return mpark::render_to_string(fragment, {.paper_mode = options.paper});
        if (options.backend == "org")
            return org::render_to_string(fragment);
        return latex::render_to_string(fragment);
    };

    // The extension is the only part of design §8's fragment-path scheme
    // that is a fact about the backend. The stem is the stable name, and
    // deriving it is Tier A's (fragments.hpp) — the same split serves all
    // three targets, which is why nothing about it lives in a backend.
    const std::string_view extension = options.backend == "mpark" ? ".md" : options.backend == "org" ? ".org" : ".tex";

    auto write_fragments = [&](const ir::Document& whole) -> std::expected<int, std::string> {
        // Validation, if it ran at all, ran over the whole document above:
        // the roster and the two document-level channels it reads describe the
        // header, not any one section of it, so splitting first would ask
        // every rule a narrower question than the one design §9 poses.
        const std::expected<std::vector<fragments::Fragment>, fragments::Error> pieces =
            fragments::split(whole, {.root = options.root});
        if (!pieces)
            return std::unexpected(std::format(
                "specgen: {}{}", pieces.error().message, pieces.error().root_unnamed ? "; name it with --root" : ""));

        std::error_code failed;
        std::filesystem::create_directories(options.split_dir, failed);
        if (failed)
            return std::unexpected(
                std::format("specgen: cannot create '{}': {}", options.split_dir, failed.message()));

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
                    std::filesystem::path(options.split_dir) / std::format("{}{}", fragment.name, extension);
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

    // Validate, then render (decision expected-error-taxonomy): an error-severity
    // finding short-circuits the rest, which is what "aborts the render" means.
    return validate_document(document, options.validate).and_then([&]() -> std::expected<int, std::string> {
        // One document in, one file or a directory of them out. Which it is,
        // is the last thing the pipeline decides, because everything above it —
        // reading, parsing, validating — is the same work either way.
        return options.split_dir.empty() ? write_output(render_document(document)) : write_fragments(document);
    });
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

    const std::string&    input   = options->input;
    const WordingOptions& wording = options->wording;

    if (input.empty()) {
        std::println(stderr, "specgen: render requires --from-ir");
        return usage(stderr, 2);
    }
    if (const std::optional<std::string> error = wording_option_error(wording)) {
        std::println(stderr, "{}", *error);
        return 2;
    }

    // Driver orchestration (decision expected-error-taxonomy) is an and_then
    // pipeline over stages, not an `if (!x) return` ladder — read the file,
    // parse it, then hand the document to the shared back half, each step
    // short-circuiting the rest on failure. All the stages share one error
    // type (a ready-to-print message) since nothing downstream of main() needs
    // to distinguish which stage failed.
    auto result = read_all_or_fail(input)
                      .and_then([&](const std::string& text) {
                          // ir::parse_document reports a ParseError; the only
                          // adaptation this stage needs is to render it in the
                          // pipeline's shared message type, which is what
                          // transform_error is for.
                          return ir::parse_document(text).transform_error([&](const ir::ParseError& error) {
                              return std::format("{}:{}: error: {}", input, error.offset, error.message);
                          });
                      })
                      .and_then([&](const ir::Document& document) { return emit_wording(document, wording); });

    if (!result) {
        std::println(stderr, "{}", result.error());
        return 1;
    }
    return *result;
}

int generate_command(const std::vector<std::string>& args) {
    // Header in, wording out, in one pass: the front end builds the document
    // and the shared back half renders it without the IR ever leaving this
    // process (decision ir-boundary -- the seam is a boundary in the code, not
    // a file the pipeline has to go through). `--emit-ir` is the other half of
    // that seam made explicit: serialize the document instead, for a consumer
    // that will render it later.
    namespace frontend = beman::specgen::frontend;

    // Everything after the first bare "--" is the clang-argument
    // tail, verbatim, with no further option parsing -- split it off before
    // the loop below ever sees it, so an "-I" meant for Clang is never
    // mistaken for one of this command's own options.
    const auto                     dash_dash = std::ranges::find(args, std::string_view("--"));
    const std::vector<std::string> head(args.begin(), dash_dash);
    const std::vector<std::string> tail(dash_dash == args.end() ? args.end() : dash_dash + 1, args.end());

    std::string    header;
    std::string    compile_commands_dir;
    WordingOptions wording;
    bool           emit_ir             = false;
    bool           no_compile_commands = false;
    // The first wording-only option seen, for the `--emit-ir` conflict below:
    // naming the one the user actually typed beats listing all five. -o is not
    // one of them -- it names where either mode's single output goes.
    std::string wording_flag;

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
        // Remembers the first wording-only option, and only the first.
        auto wording_only = [&] {
            if (wording_flag.empty())
                wording_flag = arg;
        };
        if (arg == "-h" || arg == "--help") {
            return usage(stdout, 0);
        } else if (arg == "--emit-ir") {
            emit_ir = true;
        } else if (arg == "-o" || arg == "--output") {
            if (!next(wording.output))
                return 2;
        } else if (arg == "--backend") {
            wording_only();
            if (!next(wording.backend))
                return 2;
        } else if (arg == "--split") {
            wording_only();
            if (!next(wording.split_dir))
                return 2;
        } else if (arg == "--root") {
            wording_only();
            if (!next(wording.root))
                return 2;
        } else if (arg == "--validate") {
            wording_only();
            wording.validate = true;
        } else if (arg == "--paper") {
            wording_only();
            wording.paper = true;
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

    // --emit-ir stops before the backends, so every option that steers them is
    // reported rather than ignored -- the same rule wording_option_error
    // applies within the wording options themselves.
    if (emit_ir && !wording_flag.empty()) {
        std::println(stderr, "specgen: --emit-ir writes IR, so {} does not apply", wording_flag);
        return 2;
    }
    if (header.empty()) {
        if (emit_ir) {
            std::println(stderr, "specgen: generate --emit-ir requires a header");
            return usage(stderr, 2);
        }
        if (!wording_flag.empty()) {
            std::println(stderr, "specgen: generate requires a header to render wording");
            return usage(stderr, 2);
        }

        // No header and nothing to steer: the link-proving smoke path, on a
        // stock snippet. It answers "is the Clang front end in this binary and
        // does it run", which is the one question the wording path cannot be
        // asked without a header to point it at.
        const frontend::SmokeResult smoke = frontend::smoke_check("int f(int x);");
        if (!smoke.ast_built) {
            std::println(stderr, "specgen: the Clang front end could not parse the input");
            return 1;
        }
        std::println(stderr, "specgen: Clang front-end tier OK; pass a header to generate wording");
        return 0;
    }
    if (const std::optional<std::string> error = wording_option_error(wording)) {
        std::println(stderr, "{}", *error);
        return 2;
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
        std::println(
            stderr, "specgen: using compile flags for '{}' from the database entry in '{}'", header, resolved.source);
    const frontend::ParseOptions options{
        .extra_args = resolved.args, .compile_commands_dir = {}, .probe_compile_commands = false};

    // Build, then emit, as one and_then pipeline (decision
    // expected-error-taxonomy). The stages start out with different error
    // types -- the front end reports a BuildFailure, the shared back half a
    // ready-to-print message -- so the build's is transformed into the
    // pipeline's before the chain, and a write failure then reports the way a
    // build failure does. Diagnostics collected along the way are printed but
    // never turn a successful build into a failed one.
    auto result =
        frontend::build_document(header, options)
            .transform_error(
                [](const frontend::BuildFailure& failure) { return std::format("specgen: {}", failure.message); })
            .and_then([&](beman::specgen::document_build::BuildResult built) -> std::expected<int, std::string> {
                // substrate generic algorithm
                // Formatted output, not a fold: nothing is accumulated and
                // nothing is returned, so there is no verb to name here.
                //
                // `line: severity:`: the location is a line number the
                // reader can actually find, not a byte offset, and the
                // severity is each finding's own — notes and errors are
                // not all printed as the literal word "warning". An Error
                // here still does not fail the build: failing it would
                // withhold the wording, not just the finding's own text.
                for (const auto& diagnostic : built.diagnostics)
                    std::println(stderr,
                                 "{}:{}: {}: {}",
                                 header,
                                 diagnostic.line,
                                 beman::specgen::severity_label(diagnostic.severity),
                                 diagnostic.message);

                // The single pass: the document goes straight to the backends,
                // through the same function `render` uses.
                if (!emit_ir)
                    return emit_wording(built.document, wording);

                if (wording.output.empty()) {
                    std::println(stdout, "{}", ir::emit_json(built.document));
                    return 0;
                }
                std::ofstream out(wording.output, std::ios::binary);
                if (!out)
                    return std::unexpected(std::format("specgen: cannot write '{}'", wording.output));
                out << ir::emit_json(built.document) << '\n';
                return out ? 0 : 1;
            });

    if (!result) {
        std::println(stderr, "{}", result.error());
        return 1;
    }
    return *result;
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
