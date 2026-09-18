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
//   generate    headers in, wording out, in one pass: the IR stays in memory
//               and is never written anywhere unless `--emit-ir` asks for it,
//               in which case emitting it is all the command does. Needs the
//               Clang front end, and reports so plainly when the build did
//               not include it.
//
//   dump-decls  debug mode: prints the decl/comment interleave design
//               §3.2 builds for a header. Needs the Clang front end too.
//
// Either rendering command takes a whole paper, not just a document: several
// headers or several `--from-ir` inputs render as one, validated against the
// union of their documented names, and written as the joined whole, as
// per-clause fragments, or as both (decision paper-is-the-invocation).
// `--depfile` then says what the run read to produce them, so a build can
// rebuild the wording when a header moves (decision depfile-emission).

#include <beman/specgen/backend/latex.hpp>
#include <beman/specgen/backend/mpark.hpp>
#include <beman/specgen/backend/org.hpp>
#include <beman/specgen/depfile.hpp>
#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/foundation/fold_left_short.hpp>
#include <beman/specgen/foundation/monoid.hpp>
#include <beman/specgen/fragments.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/validate/validate.hpp>

#include <beman/specgen/frontend/frontend.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <print>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
  <header>...                the headers to parse, one document each and all
                             of them one paper, in the order named; with none,
                             parse a stock snippet as a link-proving smoke
                             check
  --emit-ir                 emit the document tree (design §3.2) as IR JSON
                             for a later `render --from-ir`, instead of
                             rendering wording here. One document per file is
                             what the format is, so this takes one header
  --backend <name>          latex (default), mpark, or org
  --validate                run the wording validators before rendering; a
                             finding at error severity aborts the render
                             (exit 1) instead
  --paper                   wrap the fragment in an `::: add` editing-instruction
                             div and number its paragraphs as added (mpark only)
  --new-root <name>         drop the `.sref` class from <name> and every
                             stable name beneath it, at any depth (mpark
                             only): the paper's own proposed clause, not yet in
                             the srefs database `.sref` looks up. May be
                             repeated; a name under any of the roots given
                             loses the class
  --base-heading-level <n>  heading level of a top-level section; nested
                             sections descend from it (mpark and org only;
                             2 by default). One to six under mpark, markdown's
                             deepest heading; org has no upper limit
  --base-section-depth <n>  \rSec depth of a top-level section; nested sections
                             descend from it (latex only; 3 by default, the
                             draft's library split granularity)
  --split <dir>             write one fragment per top-level section into <dir>,
                             named from its stable name (optional.ctor.tex), and
                             list the paths written on standard output
  --root <name>             name the fragment holding the nodes outside every
                             section (--split only); derived from the sections'
                             common stable-name prefix when omitted. With
                             several headers, repeats to name each document's
                             root fragment, pairing in order
  -o, --output <file>       write the whole here instead of standard output;
                             alongside --split, write both the whole and the
                             fragments
  --depfile <file>          write a Makefile dependency fragment naming what
                             this run produced and every file its parse read,
                             with an empty rule per prerequisite so a deleted
                             header rebuilds rather than erroring. System
                             headers are left out
  --dep-target <name>       name this target in the dependency fragment
                             instead of the files actually written; repeats
                             (--depfile only)
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
  --from-ir <file>          IR JSON to read; "-" for standard input (required).
                             May be repeated, once per document of one paper:
                             --validate then runs across the union of their
                             documented names, so a name specified by a
                             sibling document is not foreign. Several inputs
                             render as one paper -- joined in order for a
                             single output, split into per-clause fragments,
                             or both -- and --root, when given at all, repeats
                             too, pairing with each --from-ir in order
  --backend <name>          latex (default), mpark, or org
  --validate                run the wording validators before rendering; a
                             finding at error severity aborts the render
                             (exit 1) instead
  --paper                   wrap the fragment in an `::: add` editing-instruction
                             div and number its paragraphs as added (mpark only)
  --new-root <name>         drop the `.sref` class from <name> and every
                             stable name beneath it, at any depth (mpark
                             only): the paper's own proposed clause, not yet in
                             the srefs database `.sref` looks up. May be
                             repeated; a name under any of the roots given
                             loses the class
  --base-heading-level <n>  heading level of a top-level section; nested
                             sections descend from it (mpark and org only;
                             2 by default). One to six under mpark, markdown's
                             deepest heading; org has no upper limit
  --base-section-depth <n>  \rSec depth of a top-level section; nested sections
                             descend from it (latex only; 3 by default, the
                             draft's library split granularity)
  --split <dir>             write one fragment per top-level section into <dir>,
                             named from its stable name (optional.ctor.tex), and
                             list the paths written on standard output
  --root <name>             name the fragment holding the nodes outside every
                             section (--split only); derived from the sections'
                             common stable-name prefix when omitted. With
                             several --from-ir inputs, repeats to name each
                             document's root fragment, pairing in order
  -o, --output <file>       write the whole here instead of standard output;
                             alongside --split, write both the whole and the
                             fragments
  --depfile <file>          write a Makefile dependency fragment naming what
                             this run produced and the IR files it read, with
                             an empty rule per prerequisite
  --dep-target <name>       name this target in the dependency fragment
                             instead of the files actually written; repeats
                             (--depfile only)

general:
  -h, --help                show this message
  --version                 show the version
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
    // --new-root <name> (mpark only), accumulated: the flag repeats, once per
    // header the paper proposes, and every occurrence adds a root (issue #94).
    std::vector<std::string> new_roots;
    // Where a top-level section starts, in the two units the backends measure
    // it in: --base-heading-level <n> (mpark and org) and
    // --base-section-depth <n> (latex). Two options rather than one because
    // the fields are two quantities and not one -- `\rSec3` is the *draft's*
    // library split granularity, while a markdown or org base is where a
    // paper's wording sits under the heading that introduces it -- which is
    // what mpark.hpp and org.hpp go out of their way to say. One flag feeding
    // both would collapse exactly that distinction (decision
    // wording-base-level, issue #97).
    //
    // Unset rather than defaulted here: what an unset flag means is the
    // backend's own default, and a copy of that number in the driver would be
    // a second place for it to drift from the header that documents it. See
    // render_document below, which reads it off a default-constructed Options.
    std::optional<int> base_heading_level;
    std::optional<int> base_section_depth;
    // --depfile <path>: where to write the Makefile dependency fragment
    // (depfile.hpp), and --dep-target <name>, accumulated, for the targets to
    // name in it instead of the files actually written. The override exists
    // because a build system's notion of what this invocation produces is not
    // always a path specgen passed: a recipe whose real target is a stamp file,
    // or one that renders into a scratch directory and installs afterwards,
    // knows its own target names and specgen cannot.
    std::string              depfile;
    std::vector<std::string> dep_targets;
};

// The driver's only integer-valued options, parsed once for both commands so a
// mistyped level reads the same from either. std::from_chars rather than
// std::stoi for the reason the parser combinators give (decision
// parser-combinators): what the user typed has to become a diagnostic naming
// the option they typed, never an exception and never a silent zero. The whole
// argument must be consumed -- `3x` is a typo, not a 3 -- and the lower bound
// lives here rather than in wording_option_error because it is a fact about
// headings rather than about any one backend: a top-level section has to *be*
// a heading, and there is no zeroth one.
std::expected<int, std::string> parse_level(std::string_view option, const std::string& text) {
    int value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size())
        return std::unexpected(std::format("specgen: {} takes a whole number, not '{}'", option, text));
    if (value < 1)
        return std::unexpected(std::format("specgen: {} must be at least 1, not {}", option, value));
    return value;
}

// `render`'s options, as the accumulator of the fold below. `awaiting` is the
// spelling of the option whose value the *next* argument supplies — what an
// index loop would express by stepping `i` past it — kept as the
// spelling rather than an enum so the "requires an argument" message can name
// the option the user actually typed.
struct RenderOptions {
    // --from-ir, accumulated: the flag repeats, once per document of the
    // paper being rendered, and validation then runs across their union of
    // documented names (issue #109).
    std::vector<std::string> inputs;
    // --root, accumulated for the same reason: with several documents each
    // needs its own root fragment name, so the flag pairs with --from-ir in
    // order. Kept beside `inputs` rather than in WordingOptions because the
    // pairing is this command's to resolve; emit_wording still sees one root.
    std::vector<std::string> roots;
    WordingOptions           wording;
    std::string              awaiting;
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
        // --new-root accumulates instead of replacing, so it is settled here
        // rather than in the reference chain below, which can only name a
        // destination the last spelling of an option overwrites (issue #94).
        if (opts.awaiting == "--new-root") {
            opts.wording.new_roots.push_back(arg);
            opts.awaiting.clear();
            return opts;
        }
        // --from-ir accumulates for the same reason: one occurrence per
        // document of the paper (issue #109), and --root pairs with it in
        // order — silently taking the last of several is the mistake
        // --new-root's history warns about (issue #94).
        if (opts.awaiting == "--from-ir") {
            opts.inputs.push_back(arg);
            opts.awaiting.clear();
            return opts;
        }
        if (opts.awaiting == "--root") {
            opts.roots.push_back(arg);
            opts.awaiting.clear();
            return opts;
        }
        // --dep-target accumulates for the same reason --new-root does: one
        // invocation can produce several files, and a rule naming only the last
        // of them would leave the others looking up to date when they are not.
        if (opts.awaiting == "--dep-target") {
            opts.wording.dep_targets.push_back(arg);
            opts.awaiting.clear();
            return opts;
        }
        // The two base-level options are settled here for the same kind of
        // reason: the reference chain below can only name a `std::string&`,
        // and these carry a number, whose failure to be one has to leave the
        // scan rather than assign through it.
        if (opts.awaiting == "--base-heading-level" || opts.awaiting == "--base-section-depth") {
            const std::expected<int, std::string> level = parse_level(opts.awaiting, arg);
            if (!level)
                return std::unexpected(OptionError{level.error()});
            std::optional<int>& target = opts.awaiting == "--base-heading-level" ? opts.wording.base_heading_level
                                                                                 : opts.wording.base_section_depth;
            target                     = *level;
            opts.awaiting.clear();
            return opts;
        }
        std::string& dest = opts.awaiting == "--backend"   ? opts.wording.backend
                            : opts.awaiting == "--split"   ? opts.wording.split_dir
                            : opts.awaiting == "--depfile" ? opts.wording.depfile
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
    if (arg == "--from-ir" || arg == "--backend" || arg == "--split" || arg == "--root" || arg == "--new-root" ||
        arg == "--base-heading-level" || arg == "--base-section-depth" || arg == "-o" || arg == "--output" ||
        arg == "--depfile" || arg == "--dep-target") {
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
    // `.sref` is an mpark/wg21 construct; the other two backends have no
    // stable-name class to drop.
    if (!options.new_roots.empty() && options.backend != "mpark")
        return std::format("specgen: --new-root applies only to the mpark backend, not '{}'", options.backend);
    // The two base-level options measure two different things, so each is a
    // usage error where the other one belongs -- and each message names the
    // one that does. Accepting either spelling everywhere would be the flag
    // that collapses the distinction, arriving by the back door; ignoring the
    // wrong one silently would leave an author believing wording was placed
    // under their own headings when it was not, which is what --paper and
    // --new-root are reported for.
    if (options.base_heading_level && options.backend == "latex")
        return std::string("specgen: --base-heading-level applies to the mpark and org backends; "
                           "the latex backend takes --base-section-depth");
    if (options.base_section_depth && options.backend != "latex")
        return std::format("specgen: --base-section-depth applies only to the latex backend, not '{}'; "
                           "that backend takes --base-heading-level",
                           options.backend);
    // Markdown stops at six heading levels. The backend saturates *descent*
    // there, because how deep a document nests is the document's business, but
    // a base is the one level the author chose, and a chosen level that did
    // nothing is worth saying out loud rather than clamping. Org has no such
    // limit, so neither does this.
    if (options.base_heading_level && *options.base_heading_level > 6 && options.backend == "mpark")
        return std::format("specgen: --base-heading-level {} is past markdown's deepest heading; "
                           "the mpark backend takes 1 to 6",
                           *options.base_heading_level);
    // --root names one of --split's derived files, so it means nothing without
    // them.
    //
    // --output alongside --split, by contrast, is no longer a contradiction and
    // is deliberately allowed: the whole and the pieces are two views of one
    // render, a paper wants both -- the assembled clause to diff against the
    // draft, and the per-clause fragments to `\input` -- and producing them
    // from one invocation is one parse and one dependency fragment rather than
    // two of each.
    if (!options.root.empty() && options.split_dir.empty())
        return std::string("specgen: --root names a fragment, so it applies only with --split");
    // A dependency fragment whose rule has no target is not a rule. specgen
    // knows the targets when it wrote the files itself; writing to standard
    // output it does not, and guessing is worse than asking.
    if (!options.depfile.empty() && options.output.empty() && options.split_dir.empty() && options.dep_targets.empty())
        return std::string("specgen: --depfile names what this run produced, so it needs -o, --split, "
                           "or --dep-target");
    if (!options.dep_targets.empty() && options.depfile.empty())
        return std::string("specgen: --dep-target names a target in a dependency fragment, so it applies "
                           "only with --depfile");
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

// The backend seam (decision ir-boundary): one IR, one call, chosen by
// name. Validated by wording_option_error above, so there is no fourth
// case. A free function rather than a detail of emit_wording because the
// paper path below renders fragments of several documents through the same
// options (issue #109), and two spellings of the seam would be two places
// for them to drift apart.
//
// An unset base level is the backend's own default, read off a
// default-constructed Options rather than restated here: the number an
// omitted flag means is documented in the backend's header, and a copy of
// it in the driver would be a second place for it to drift from.
//
// This function is also the only place any path renders, so --split needs
// nothing of its own: a fragment *is* a document (design §8), and it
// therefore starts at the same base a whole render does. The alternative --
// a fragment measuring from its own top-level section -- would make the
// base mean one thing in a document and another in a piece of one.
std::string render_wording(const ir::Document& fragment, const WordingOptions& options) {
    if (options.backend == "mpark")
        return mpark::render_to_string(
            fragment,
            {.base_heading_level = options.base_heading_level.value_or(mpark::Options{}.base_heading_level),
             .paper_mode         = options.paper,
             .new_roots          = options.new_roots});
    if (options.backend == "org")
        return org::render_to_string(
            fragment, {.base_heading_level = options.base_heading_level.value_or(org::Options{}.base_heading_level)});
    return latex::render_to_string(
        fragment, {.base_section_depth = options.base_section_depth.value_or(latex::Options{}.base_section_depth)});
}

// The extension is the only part of design §8's fragment-path scheme
// that is a fact about the backend. The stem is the stable name, and
// deriving it is Tier A's (fragments.hpp) — the same split serves all
// three targets, which is why nothing about it lives in a backend.
std::string_view fragment_extension(const WordingOptions& options) {
    return options.backend == "mpark" ? ".md" : options.backend == "org" ? ".org" : ".tex";
}

// The files an emit step wrote, in the order it wrote them. Two readers: the
// `--split` manifest on standard output, and `--depfile`'s target list. Empty
// means the wording went to standard output, which is not a file anything can
// depend on.
using Written = std::vector<std::string>;

// The manifest: one path per line. Built whole and printed once (decision
// format-print-output), so a failed write leaves no half-list claiming files
// that are not there.
std::string manifest_text(const Written& paths) {
    return paths | std::views::transform([](const std::string& path) { return std::format("{}\n", path); }) |
           std::views::join | std::ranges::to<std::string>();
}

// The Makefile dependency fragment (depfile.hpp), written last of all -- after
// every output it names is on disk. A `.d` promising files that were never
// written is worse than none: make believes the next build has nothing to do.
//
// A caller that gave no `--depfile` gets no fragment and no error; the option's
// absence is not a failure, and every emit path calls this unconditionally
// rather than each remembering to ask first.
std::expected<void, std::string>
write_depfile(const WordingOptions& options, const Written& written, const std::vector<std::string>& prerequisites) {
    if (options.depfile.empty())
        return {};
    // wording_option_error has already refused the case where neither says
    // anything, so one of the two is non-empty here.
    const Written& targets = options.dep_targets.empty() ? written : options.dep_targets;

    std::ofstream out(options.depfile, std::ios::binary);
    if (!out)
        return std::unexpected(std::format("specgen: cannot write '{}'", options.depfile));
    out << beman::specgen::depfile::format(targets, prerequisites);
    if (!out)
        return std::unexpected(std::format("specgen: cannot write '{}'", options.depfile));
    return {};
}

// One rendered string to wherever the caller asked for it: `-o` if it named a
// file, standard output otherwise. Bulk I/O, not formatting -- `{}` on stdout
// and `<<` on the file stream, rather than running a whole rendered document
// back through the format machinery (decision format-print-output's boundary
// rule).
std::expected<Written, std::string> write_whole(const std::string& rendered, const WordingOptions& options) {
    if (options.output.empty()) {
        std::print(stdout, "{}", rendered);
        return Written{};
    }
    std::ofstream out(options.output, std::ios::binary);
    if (!out)
        return std::unexpected(std::format("specgen: cannot write '{}'", options.output));
    out << rendered;
    if (!out)
        return std::unexpected(std::format("specgen: cannot write '{}'", options.output));
    return Written{options.output};
}

// One file per fragment into `--split`'s directory, then the manifest.
//
// Takes the already-split pieces rather than a document, because the two
// callers arrive with different things to say about a failure: one document's
// split can ask for `--root`, while a paper's has to name which of its inputs
// the unnamed fragment came from. Splitting is theirs; writing is the same
// either way.
std::expected<Written, std::string> write_fragments(const std::vector<fragments::Fragment>& pieces,
                                                    const WordingOptions&                   options) {
    std::error_code failed;
    std::filesystem::create_directories(options.split_dir, failed);
    if (failed)
        return std::unexpected(std::format("specgen: cannot create '{}': {}", options.split_dir, failed.message()));

    // The early-stop verb again (decision expected-error-taxonomy): writing a
    // set of files is a sequence that must stop at the first failure, and what
    // it accumulates is the paths in document order, which is the one thing the
    // directory listing loses. The manifest is printed once at the end rather
    // than a line at a time (decision format-print-output), so a failed write
    // leaves no half-list claiming files that are not there.
    return beman::specgen::foundation::fold_left_short(
               pieces,
               Written{},
               [&](Written listing, const fragments::Fragment& fragment) -> std::expected<Written, std::string> {
                   const std::filesystem::path path = std::filesystem::path(options.split_dir) /
                                                      std::format("{}{}", fragment.name, fragment_extension(options));
                   std::ofstream               out(path, std::ios::binary);
                   if (!out)
                       return std::unexpected(std::format("specgen: cannot write '{}'", path.string()));
                   out << render_wording(fragment.document, options);
                   if (!out)
                       return std::unexpected(std::format("specgen: cannot write '{}'", path.string()));
                   listing.push_back(path.generic_string());
                   return listing;
               })
        .transform([](Written paths) {
            std::print(stdout, "{}", manifest_text(paths));
            return paths;
        });
}

// Whether this run writes the whole document at all. `--split` on its own means
// the pieces *are* the output, and printing the whole to standard output beside
// them would bury the manifest that says where they went. Naming `-o` as well
// asks for both, and gets both.
bool wants_whole(const WordingOptions& options) { return options.split_dir.empty() || !options.output.empty(); }

// The back half of the driver: one document in, one file -- or a directory of
// them, or both -- out. `render` arrives here from IR JSON and `generate` from
// a header it has just parsed, and nothing below can tell which, so the
// single-pass wording is byte-identical to the two-pass route by construction
// rather than by comparison.
std::expected<Written, std::string> emit_wording(const ir::Document& document, const WordingOptions& options) {
    // Validate, then render (decision expected-error-taxonomy): an
    // error-severity finding short-circuits the rest, which is what "aborts the
    // render" means.
    return validate_document(document, options.validate)
        .and_then([&]() -> std::expected<Written, std::string> {
            return wants_whole(options) ? write_whole(render_wording(document, options), options)
                                        : std::expected<Written, std::string>{Written{}};
        })
        .and_then([&](Written written) -> std::expected<Written, std::string> {
            if (options.split_dir.empty())
                return written;
            // Validation, if it ran at all, ran over the whole document above:
            // the roster and the two document-level channels it reads describe
            // the header, not any one section of it, so splitting first would
            // ask every rule a narrower question than the one design §9 poses.
            const std::expected<std::vector<fragments::Fragment>, fragments::Error> pieces =
                fragments::split(document, {.root = options.root});
            if (!pieces)
                return std::unexpected(std::format("specgen: {}{}",
                                                   pieces.error().message,
                                                   pieces.error().root_unnamed ? "; name it with --root" : ""));
            return write_fragments(*pieces, options).transform([&](Written written_pieces) {
                written.append_range(std::move(written_pieces));
                return std::move(written);
            });
        });
}

// Several documents are one paper (issue #109). Validation runs across all of
// them, against the union of their documented names — a name specified by a
// sibling document is not foreign — and an error in any aborts the render of
// every one: the unit that has to be internally consistent is the paper, so
// nothing is written from an inconsistent one. Each finding is prefixed with
// the input it came from, since a context path alone no longer says which
// document it is about.
//
// The same report-or-abort shape validate_document gives one document:
// warnings are printed here and rendering continues, any error returns every
// finding for the caller to print and renders nothing.
std::expected<void, std::string>
validate_paper(const std::vector<std::string>& labels, const std::vector<ir::Document>& documents, bool run) {
    if (!run)
        return {};

    const auto set_union = beman::specgen::foundation::monoid{
        [](std::set<std::string> a, const std::set<std::string>& b) {
            a.insert(b.begin(), b.end());
            return a;
        },
        std::set<std::string>{},
    };
    const std::set<std::string> paper = beman::specgen::foundation::mconcat_map(
        documents, [](const ir::Document& document) { return validate::documented_names(document); }, set_union);

    struct PaperFindings {
        std::vector<std::string> lines;
        bool                     errors = false;
    };
    const auto findings_monoid = beman::specgen::foundation::monoid{
        [](PaperFindings a, const PaperFindings& b) {
            a.lines.insert(a.lines.end(), b.lines.begin(), b.lines.end());
            a.errors = a.errors || b.errors;
            return a;
        },
        PaperFindings{},
    };
    const PaperFindings findings = beman::specgen::foundation::mconcat_map(
        std::views::zip(labels, documents),
        [&](const auto& pair) {
            const auto& [label, document]           = pair;
            const validate::Diagnostics diagnostics = validate::validate(document, paper);
            return PaperFindings{
                diagnostics | std::views::transform([&](const validate::Diagnostic& finding) {
                    return std::format("specgen: {}: {}", label, validate::format_diagnostic(finding));
                }) | std::ranges::to<std::vector<std::string>>(),
                validate::has_errors(diagnostics),
            };
        },
        findings_monoid);

    if (findings.errors)
        return std::unexpected(findings.lines | std::views::join_with('\n') | std::ranges::to<std::string>());
    // substrate generic algorithm: formatted output, not a fold — nothing is
    // accumulated and nothing is returned.
    for (const std::string& line : findings.lines)
        std::println(stderr, "{}", line);
    return {};
}

// The back half again, for a paper of several documents. `render` arrives here
// with one `--from-ir` per document and `generate` with one header per
// document, and as with emit_wording nothing below can tell which.
//
// `labels` names each document for a diagnostic — an IR path or a header path —
// and pairs with `documents` and, when it is given at all, with `roots`.
std::expected<Written, std::string> emit_paper(const std::vector<std::string>&  labels,
                                               const std::vector<ir::Document>& documents,
                                               const std::vector<std::string>&  roots,
                                               const WordingOptions&            options) {
    // The whole paper is its documents rendered in order and joined by a blank
    // line. That is the same text `--split` would write, in the same order,
    // with the fragment boundaries left out — which is what makes the assembled
    // clause a thing a paper can diff against the draft rather than a thing
    // some script reassembled and might have reassembled differently.
    const auto write_paper = [&]() -> std::expected<Written, std::string> {
        if (!wants_whole(options))
            return Written{};
        return write_whole(documents | std::views::transform([&](const ir::Document& document) {
                               return render_wording(document, options);
                           }) | std::views::join_with(std::string_view("\n")) |
                               std::ranges::to<std::string>(),
                           options);
    };

    // Split every document before writing anything: the fragments land in one
    // --split directory, so two documents deriving the same name would
    // otherwise have the later one silently overwrite the earlier — and a
    // paper's headers routinely share a stable-name prefix, which is exactly
    // the derived root name. Splitting first turns that into a reported
    // collision, and pairs each document with its own --root.
    struct PaperFragment {
        std::string         label;
        fragments::Fragment fragment;
    };
    const auto split_paper = [&]() -> std::expected<std::vector<PaperFragment>, std::string> {
        return beman::specgen::foundation::fold_left_short(
            std::views::zip(labels, documents, std::views::iota(std::size_t{0})),
            std::vector<PaperFragment>{},
            [&](std::vector<PaperFragment> flat,
                const auto&                triple) -> std::expected<std::vector<PaperFragment>, std::string> {
                const auto& [label, document, index] = triple;
                const std::string root               = roots.empty() ? std::string{} : roots[index];
                return fragments::split(document, {.root = root})
                    .transform_error([&](const fragments::Error& error) {
                        return std::format("specgen: {}: {}{}",
                                           label,
                                           error.message,
                                           error.root_unnamed ? "; name it with --root (one per document, in order)"
                                                              : "");
                    })
                    .transform([&](std::vector<fragments::Fragment> split_pieces) {
                        // substrate generic algorithm: append with provenance;
                        // there is no single-range verb that tags each element
                        // with the input it came from while moving it.
                        for (fragments::Fragment& piece : split_pieces)
                            flat.push_back(PaperFragment{label, std::move(piece)});
                        return std::move(flat);
                    });
            });
    };

    const auto check_collisions = [&](const std::vector<PaperFragment>& pieces) -> std::expected<void, std::string> {
        return beman::specgen::foundation::fold_left_short(
                   pieces,
                   std::map<std::string, std::string>{},
                   [&](std::map<std::string, std::string> seen,
                       const PaperFragment& piece) -> std::expected<std::map<std::string, std::string>, std::string> {
                       const auto [entry, inserted] = seen.emplace(piece.fragment.name, piece.label);
                       if (!inserted)
                           return std::unexpected(
                               std::format("specgen: fragment `{}{}` is written by both `{}` and `{}`; "
                                           "give each document its own --root",
                                           piece.fragment.name,
                                           fragment_extension(options),
                                           entry->second,
                                           piece.label));
                       return seen;
                   })
            .transform([](const std::map<std::string, std::string>&) {});
    };

    return validate_paper(labels, documents, options.validate)
        .and_then(write_paper)
        .and_then([&](Written written) -> std::expected<Written, std::string> {
            if (options.split_dir.empty())
                return written;
            return split_paper()
                .and_then([&](std::vector<PaperFragment> pieces) -> std::expected<Written, std::string> {
                    return check_collisions(pieces).and_then([&] {
                        // Moved out rather than copied: a paper's documents are
                        // whole headers' worth of IR, and write_fragments needs
                        // only the pieces, not which input each came from.
                        return write_fragments(pieces | std::views::transform([](PaperFragment& piece) {
                                                   return std::move(piece.fragment);
                                               }) | std::ranges::to<std::vector<fragments::Fragment>>(),
                                               options);
                    });
                })
                .transform([&](Written written_pieces) {
                    written.append_range(std::move(written_pieces));
                    return std::move(written);
                });
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

    const std::vector<std::string>& inputs  = options->inputs;
    const std::vector<std::string>& roots   = options->roots;
    WordingOptions                  wording = options->wording;

    if (inputs.empty()) {
        std::println(stderr, "specgen: render requires --from-ir");
        return usage(stderr, 2);
    }
    // The pairing constraints live here rather than in wording_option_error,
    // which sees only the wording half: they are facts about how many inputs
    // there are, not about any backend. One input takes at most one --root —
    // silently keeping the last of several is the mistake --new-root's
    // history warns about (issue #94) — and several inputs take none or
    // exactly one each, pairing in order.
    if (inputs.size() == 1 && roots.size() > 1) {
        std::println(stderr, "specgen: one --from-ir takes one --root, not {}", roots.size());
        return 2;
    }
    if (inputs.size() > 1 && !roots.empty() && roots.size() != inputs.size()) {
        std::println(stderr,
                     "specgen: --root pairs with --from-ir in order, so pass one per input or none: "
                     "{} input(s), {} --root(s)",
                     inputs.size(),
                     roots.size());
        return 2;
    }
    // The paper path's own copy of wording_option_error's --root rule, which
    // cannot see these: the single-document path assigns its one root into
    // WordingOptions just below and is checked there, while a paper's roots
    // stay in the list that pairs with the inputs.
    if (inputs.size() > 1 && !roots.empty() && wording.split_dir.empty()) {
        std::println(stderr, "specgen: --root names a fragment, so it applies only with --split");
        return 2;
    }
    // The single-document path reads its root off WordingOptions, exactly as
    // it always has; the paper path pairs below instead.
    if (inputs.size() == 1 && roots.size() == 1)
        wording.root = roots.front();
    if (const std::optional<std::string> error = wording_option_error(wording)) {
        std::println(stderr, "{}", *error);
        return 2;
    }

    // Read and parse every input before anything validates or renders: a
    // paper is the unit (issue #109), and half its fragments on disk beside
    // an unreadable other half is the state this ordering exists to prevent.
    // fold_left_short, not a loop: the scan must stop at the first input
    // that fails to read or parse and look at no later one.
    const auto documents = beman::specgen::foundation::fold_left_short(
        inputs,
        std::vector<ir::Document>{},
        [](std::vector<ir::Document> docs,
           const std::string&        input) -> std::expected<std::vector<ir::Document>, std::string> {
            return read_all_or_fail(input)
                .and_then([&](const std::string& text) {
                    // ir::parse_document reports a ParseError; the only
                    // adaptation this stage needs is to render it in the
                    // pipeline's shared message type, which is what
                    // transform_error is for.
                    return ir::parse_document(text).transform_error([&](const ir::ParseError& error) {
                        return std::format("{}:{}: error: {}", input, error.offset, error.message);
                    });
                })
                .transform([&](ir::Document document) {
                    docs.push_back(std::move(document));
                    return std::move(docs);
                });
        });
    if (!documents) {
        std::println(stderr, "{}", documents.error());
        return 1;
    }

    // One document is exactly the pipeline this command has always been:
    // emit_wording validates and renders it, and the output is byte-identical
    // to what it was before --from-ir learned to repeat.
    if (documents->size() == 1) {
        const auto result = emit_wording(documents->front(), wording);
        if (!result) {
            std::println(stderr, "{}", result.error());
            return 1;
        }
        if (const auto failed = write_depfile(wording, *result, inputs); !failed) {
            std::println(stderr, "{}", failed.error());
            return 1;
        }
        return 0;
    }

    const auto written = emit_paper(inputs, *documents, roots, wording);
    if (!written) {
        std::println(stderr, "{}", written.error());
        return 1;
    }
    // The prerequisites of a render are the IR files it read. Modest on its
    // own, and the point: a two-stage build whose `generate --emit-ir` step
    // already wrote a fragment naming the headers now has both edges, and make
    // chains them without either stage knowing about the other.
    if (const auto failed = write_depfile(wording, *written, inputs); !failed) {
        std::println(stderr, "{}", failed.error());
        return 1;
    }
    return 0;
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

    // The headers, in the order they were named: one document each, and all of
    // them one paper. The order is the paper's clause order, which is why they
    // accumulate rather than the last one winning.
    std::vector<std::string> headers;
    // --root, accumulated and pairing with `headers` in order, exactly as
    // `render`'s pairs with --from-ir: with several documents each needs its
    // own root fragment name.
    std::vector<std::string> roots;
    std::string              compile_commands_dir;
    WordingOptions           wording;
    bool                     emit_ir             = false;
    bool                     no_compile_commands = false;
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
            // One root per occurrence, pairing with the headers in order
            // (issue #94's rule again): silently keeping the last of several
            // would leave every earlier document's root fragment unnamed.
            std::string root;
            if (!next(root))
                return 2;
            roots.push_back(std::move(root));
        } else if (arg == "--validate") {
            wording_only();
            wording.validate = true;
        } else if (arg == "--paper") {
            wording_only();
            wording.paper = true;
        } else if (arg == "--new-root") {
            wording_only();
            // One root per occurrence (issue #94): `next` fills a string, and
            // the string joins the list rather than replacing it.
            std::string new_root;
            if (!next(new_root))
                return 2;
            wording.new_roots.push_back(std::move(new_root));
        } else if (arg == "--base-heading-level" || arg == "--base-section-depth") {
            wording_only();
            // `next` fills a string; the number comes out of the same helper
            // `render` uses, so a mistyped level reads identically from either
            // command rather than from two hand-written conversions.
            std::string text;
            if (!next(text))
                return 2;
            const std::expected<int, std::string> level = parse_level(arg, text);
            if (!level) {
                std::println(stderr, "{}", level.error());
                return 2;
            }
            std::optional<int>& target =
                arg == "--base-heading-level" ? wording.base_heading_level : wording.base_section_depth;
            target = *level;
        } else if (arg == "--depfile") {
            // Not a wording-only option: it describes what this run produced,
            // which --emit-ir produces too (the IR file is as much a build
            // artifact of the header as the wording is).
            if (!next(wording.depfile))
                return 2;
        } else if (arg == "--dep-target") {
            std::string dep_target;
            if (!next(dep_target))
                return 2;
            wording.dep_targets.push_back(std::move(dep_target));
        } else if (arg == "--compile-commands") {
            if (!next(compile_commands_dir))
                return 2;
        } else if (arg == "--no-compile-commands") {
            no_compile_commands = true;
        } else if (!arg.empty() && arg.front() != '-') {
            headers.push_back(arg);
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
    // One IR document per file is what the format is; there is no envelope for
    // several, and `render --from-ir` takes them one path at a time. A paper's
    // worth of IR is therefore a `generate --emit-ir` per header, which is what
    // the two-stage route has always been -- and is now only needed when the IR
    // itself is wanted, since one `generate` renders the paper directly.
    if (emit_ir && headers.size() > 1) {
        std::println(stderr,
                     "specgen: --emit-ir writes one document, so it takes one header, not {}; "
                     "run it once per header",
                     headers.size());
        return 2;
    }
    // The same pairing `render` requires of --root and --from-ir, for the same
    // reason (issue #94): one per document, in order, or none at all.
    if (headers.size() == 1 && roots.size() > 1) {
        std::println(stderr, "specgen: one header takes one --root, not {}", roots.size());
        return 2;
    }
    if (headers.size() > 1 && !roots.empty() && roots.size() != headers.size()) {
        std::println(stderr,
                     "specgen: --root pairs with the headers in order, so pass one per header or none: "
                     "{} header(s), {} --root(s)",
                     headers.size(),
                     roots.size());
        return 2;
    }
    if (headers.size() > 1 && !roots.empty() && wording.split_dir.empty()) {
        std::println(stderr, "specgen: --root names a fragment, so it applies only with --split");
        return 2;
    }
    // The single-document path reads its root off WordingOptions, exactly as it
    // always has; the paper path pairs below instead.
    if (headers.size() == 1 && roots.size() == 1)
        wording.root = roots.front();
    if (headers.empty()) {
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
    // Resolved per header, not once for the paper: a compilation database
    // answers about a file, and two headers of one paper can perfectly well
    // have been compiled with different flags. Papers whose headers share a
    // `--` tail -- every one so far -- get the identical answer each time, so
    // the single-header route through here is unchanged.
    const auto resolve_for = [&](const std::string& header) {
        const frontend::ResolvedArgs resolved = frontend::resolve_extra_args(header, raw_options);
        if (!resolved.source.empty())
            std::println(stderr,
                         "specgen: using compile flags for '{}' from the database entry in '{}'",
                         header,
                         resolved.source);
        return frontend::ParseOptions{
            .extra_args = resolved.args, .compile_commands_dir = {}, .probe_compile_commands = false};
    };

    // Build, then emit, as one and_then pipeline (decision
    // expected-error-taxonomy). The stages start out with different error
    // types -- the front end reports a BuildFailure, the shared back half a
    // ready-to-print message -- so the build's is transformed into the
    // pipeline's before the chain, and a write failure then reports the way a
    // build failure does. Diagnostics collected along the way are printed but
    // never turn a successful build into a failed one.
    //
    // Every header is parsed before anything is written, for the reason
    // `render` reads every `--from-ir` first: a paper is the unit (issue #109),
    // and half its wording on disk beside a header that would not parse is the
    // state this ordering exists to prevent. fold_left_short, so the scan stops
    // at the first header that fails and parses no later one.
    auto result =
        beman::specgen::foundation::fold_left_short(
            headers,
            std::vector<frontend::DocumentBuild>{},
            [&](std::vector<frontend::DocumentBuild> built,
                const std::string& header) -> std::expected<std::vector<frontend::DocumentBuild>, std::string> {
                return frontend::build_document_with_sources(header, resolve_for(header))
                    .transform_error([](const frontend::BuildFailure& failure) {
                        return std::format("specgen: {}", failure.message);
                    })
                    .transform([&](frontend::DocumentBuild one) {
                        // substrate generic algorithm
                        // Formatted output, not a fold: nothing is accumulated
                        // and nothing is returned, so there is no verb to name
                        // here.
                        //
                        // `line: severity:`: the location is a line number the
                        // reader can actually find, not a byte offset, and the
                        // severity is each finding's own — notes and errors are
                        // not all printed as the literal word "warning". An
                        // Error here still does not fail the build: failing it
                        // would withhold the wording, not just the finding's
                        // own text. `diagnostic.file` is set only for a finding
                        // in a header the document follows (a `.syn` region's
                        // includes), where the main file's name would be the
                        // wrong one — which is also what already tells a
                        // paper's findings apart, with no per-document prefix
                        // needed.
                        for (const auto& diagnostic : one.result.diagnostics)
                            std::println(stderr,
                                         "{}:{}: {}: {}",
                                         diagnostic.file.empty() ? header : diagnostic.file,
                                         diagnostic.line,
                                         beman::specgen::severity_label(diagnostic.severity),
                                         diagnostic.message);
                        built.push_back(std::move(one));
                        return std::move(built);
                    });
            })
            .and_then([&](std::vector<frontend::DocumentBuild> built) -> std::expected<int, std::string> {
                // `--emit-ir` stops at the seam (decision ir-boundary): the IR
                // file is the output, and is as much a build artifact of the
                // header as the wording is -- so it gets a dependency fragment
                // on the same terms, which is the first edge of a two-stage
                // build whose `render --from-ir` step supplies the second.
                const auto write_ir = [&]() -> std::expected<Written, std::string> {
                    const ir::Document& document = built.front().result.document; // exactly one, enforced above
                    if (wording.output.empty()) {
                        std::println(stdout, "{}", ir::emit_json(document));
                        return Written{};
                    }
                    std::ofstream out(wording.output, std::ios::binary);
                    if (!out)
                        return std::unexpected(std::format("specgen: cannot write '{}'", wording.output));
                    out << ir::emit_json(document) << '\n';
                    if (!out)
                        return std::unexpected(std::format("specgen: cannot write '{}'", wording.output));
                    return Written{wording.output};
                };

                // The single pass: the documents go straight to the backends,
                // through the same two functions `render` uses -- which is what
                // makes the one-pass wording byte-identical to the two-pass
                // route by construction rather than by comparison.
                const auto write_wording = [&]() -> std::expected<Written, std::string> {
                    std::vector<ir::Document> documents = built |
                                                          std::views::transform([](frontend::DocumentBuild& one) {
                                                              return std::move(one.result.document);
                                                          }) |
                                                          std::ranges::to<std::vector<ir::Document>>();
                    return documents.size() == 1 ? emit_wording(documents.front(), wording)
                                                 : emit_paper(headers, documents, roots, wording);
                };

                return (emit_ir ? write_ir() : write_wording())
                    .and_then([&](const Written& written) -> std::expected<int, std::string> {
                        // Every file every parse read, which is what the output
                        // is a build artifact of. Sorted and deduplicated
                        // across the paper: two headers of one paper routinely
                        // include the same third one, and make has no use for
                        // hearing so twice.
                        std::vector<std::string> prerequisites =
                            built | std::views::transform(&frontend::DocumentBuild::sources) | std::views::join |
                            std::ranges::to<std::vector<std::string>>();
                        std::ranges::sort(prerequisites);
                        const auto duplicates = std::ranges::unique(prerequisites);
                        prerequisites.erase(duplicates.begin(), duplicates.end());
                        return write_depfile(wording, written, prerequisites).transform([] { return 0; });
                    });
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
