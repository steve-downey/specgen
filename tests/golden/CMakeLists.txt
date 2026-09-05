# tests/golden/CMakeLists.txt                                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Golden cases: checked-in input in, checked-in wording or IR out, compared
# byte for byte. `specgen_add_golden()` is the one registration path; every
# case below is just its distinguishing arguments.
set(GOLDEN_UPDATE_COMMANDS "")

# specgen_add_golden(<case> INPUT <path> [MODE generate|validate]
#                    [BACKEND <name>] [NO_VALIDATE]
#                    [EXTRA_ARGS <clang args>] [COMPILE_COMMANDS_DIR <dir>]
#                    [EXPECTED_DIR <dir>])
#
# Registers `golden.<case>` (run specgen, compare its output byte for byte
# against the checked-in expected file) and appends the matching
# regeneration step to GOLDEN_UPDATE_COMMANDS (propagated to the caller's
# scope), so `make goldens` stays wholesale.
#
# MODE omitted (render, the default): INPUT is checked-in IR JSON, run
# through `specgen render --backend latex`, compared against
# <case>/expected.tex. Runs on every lane.
#
# PAPER (render mode, mpark only): add `--paper`, so the fragment comes
# out wrapped in an `::: add` div with added-paragraph numbering.
#
# BACKEND (render mode only): render through a non-default backend. The
# test registers as `golden.<case>.<backend>` and compares against that
# backend's own extension in the same case directory (mpark -> expected.md,
# org -> expected.org), so one input renders to one expected file per backend
# and a case exercising three backends is three calls sharing a directory.
#
# MODE generate: INPUT is a corpus header, run through
# `specgen generate --emit-ir`, compared against <case>/expected.json.
# Needs the front end, which every build has.
# run-golden.cmake always adds `--no-compile-commands` to this invocation: the golden harness must never probe for a
# compile_commands.json, since a gitignored one at the repo root would make a
# generate-mode case's parse depend on whether a developer happens to have a
# build configured. Two arguments let a case supply compiler arguments
# instead, the two ways the front end accepts them:
#
# EXTRA_ARGS (generate mode only): a single string of clang arguments,
# space-split by run-golden.cmake and passed after a `--`, e.g.
# `EXTRA_ARGS "-I ${dir}"`. Exercises the `--` tail.
#
# COMPILE_COMMANDS_DIR (generate mode only): a directory holding a
# compile_commands.json, passed as `--compile-commands <dir>`. Exercises the
# database path. Generate this directory's compile_commands.json with
# `file(GENERATE ...)`/`configure_file()` before registering the case that
# names it.
#
# EXPECTED_DIR (generate mode only): read `expected.json` from this
# directory instead of `<case>/`, so a second way of reaching one parse (e.g.
# EXTRA_ARGS vs. COMPILE_COMMANDS_DIR) can be pinned against the *same*
# checked-in IR as the first — the strongest form of "these two are the same
# parse", since a diff between them would have to be a real difference rather
# than two golden files that merely happen to agree.
#
# MODE validate: INPUT is checked-in IR JSON, run through
# `specgen render --from-ir INPUT --validate`, its stderr compared against
# <case>/expected.diag (run-golden.cmake requires exit code 1, since a
# reported finding is the point of this mode). Needs no Clang, so it runs on
# every lane like the render-mode cases.
#
# MODE diagnose: INPUT is a corpus header, run through the same
# `generate --emit-ir` the generate mode uses, but with its *stderr* compared
# against <case>/expected.diag and the IR discarded — the build-time findings
# as golden text. Clang-tier only, like generate.
#
# MODE split: INPUT is checked-in IR JSON, run through
# `specgen render --split` twice into one scratch directory; the fragments are
# compared against <case>/expected.<backend>/ and the printed manifest against
# <case>/expected.<backend>.manifest. BACKEND is required (it picks the
# extension as well as the expected directory) and ROOT, if given, is passed
# through as `--root`. Needs no Clang, like render.
function(specgen_add_golden case)
    set(one_value_args
        INPUT
        MODE
        BACKEND
        EXPECT_EXIT
        ROOT
        EXTRA_ARGS
        COMPILE_COMMANDS_DIR
        EXPECTED_DIR
    )
    cmake_parse_arguments(
        ARG
        "NO_VALIDATE;PAPER"
        "${one_value_args}"
        ""
        ${ARGN}
    )

    # A backend-qualified case shares its directory with the default-backend
    # one, so the test name and the scratch file carry the suffix too.
    set(test_name "golden.${case}")
    set(stem "${case}")
    if(ARG_BACKEND)
        set(test_name "golden.${case}.${ARG_BACKEND}")
        set(stem "${case}.${ARG_BACKEND}")
    endif()

    set(case_dir "${CMAKE_CURRENT_SOURCE_DIR}/${case}")
    # EXPECTED_DIR: read expected.json from a different case's
    # directory than this one's, so a second golden reaching the same parse a
    # different way can be pinned against the exact same checked-in IR.
    set(expected_dir "${case_dir}")
    if(ARG_EXPECTED_DIR)
        set(expected_dir "${ARG_EXPECTED_DIR}")
    endif()
    if(ARG_MODE STREQUAL "split")
        # A directory of fragments, not a file; ACTUAL is the scratch root the
        # driver is run from, so the manifest it prints stays relative.
        set(expected "${case_dir}/expected.${ARG_BACKEND}")
        set(actual "${CMAKE_CURRENT_BINARY_DIR}/${stem}.split")
    elseif(ARG_MODE STREQUAL "generate")
        set(expected "${expected_dir}/expected.json")
        set(actual "${CMAKE_CURRENT_BINARY_DIR}/${stem}.json")
    elseif(ARG_MODE STREQUAL "diagnose" OR ARG_MODE STREQUAL "validate")
        set(expected "${case_dir}/expected.diag")
        set(actual "${CMAKE_CURRENT_BINARY_DIR}/${stem}.diag")
    elseif(ARG_BACKEND STREQUAL "mpark")
        set(expected "${case_dir}/expected.md")
        set(actual "${CMAKE_CURRENT_BINARY_DIR}/${stem}.md")
    elseif(ARG_BACKEND STREQUAL "org")
        set(expected "${case_dir}/expected.org")
        set(actual "${CMAKE_CURRENT_BINARY_DIR}/${stem}.org")
    else()
        set(expected "${case_dir}/expected.tex")
        set(actual "${CMAKE_CURRENT_BINARY_DIR}/${stem}.tex")
    endif()

    set(case_args
        "-DSPECGEN=$<TARGET_FILE:specgen>"
        "-DINPUT=${ARG_INPUT}"
        "-DEXPECTED=${expected}"
        "-DACTUAL=${actual}"
    )
    if(ARG_MODE)
        list(APPEND case_args "-DMODE=${ARG_MODE}")
    endif()
    if(ARG_BACKEND)
        list(APPEND case_args "-DBACKEND=${ARG_BACKEND}")
    endif()
    if(ARG_PAPER)
        list(APPEND case_args "-DPAPER=ON")
    endif()
    if(ARG_ROOT)
        list(APPEND case_args "-DROOT=${ARG_ROOT}")
    endif()
    if(DEFINED ARG_EXPECT_EXIT)
        list(APPEND case_args "-DEXPECT_EXIT=${ARG_EXPECT_EXIT}")
    endif()
    if(ARG_EXTRA_ARGS)
        list(APPEND case_args "-DEXTRA_ARGS=${ARG_EXTRA_ARGS}")
    endif()
    if(ARG_COMPILE_COMMANDS_DIR)
        list(
            APPEND case_args
            "-DCOMPILE_COMMANDS_DIR=${ARG_COMPILE_COMMANDS_DIR}"
        )
    endif()

    add_test(
        NAME ${test_name}
        COMMAND
            ${CMAKE_COMMAND} ${case_args} -P
            "${CMAKE_CURRENT_SOURCE_DIR}/run-golden.cmake"
    )

    # Every generate-mode golden round-trips: the IR JSON checked in as
    # expected.json must read back and render. This gate reads the *checked-in*
    # expected file, not this run's output, which keeps the case independent
    # of test ordering. A nonzero exit fails the test, so no comparison script
    # is needed; the rendered text is written to the build tree and discarded.
    #
    # The `.validate` sibling is the same idea one step further: every
    # corpus header satisfies design §9's coverage invariant, so `--validate`
    # over its checked-in IR must be silent. This is what keeps the corpus
    # honest — without a gate, a header that declares an entity and never
    # specifies it would go unnoticed.
    #
    # NO_VALIDATE is the opt-out: a corpus header written *deliberately* to
    # violate a validator skips the silence demand. It is not a way to stop
    # checking — `spec_namespace.hpp`, the one header that takes it, is
    # validated by a MODE validate case over this same expected.json, which
    # pins the finding as golden text. Demanding the exact diagnostic is a
    # stronger gate than demanding none.
    if(ARG_MODE STREQUAL "generate")
        add_test(
            NAME golden.${case}.roundtrip
            COMMAND
                $<TARGET_FILE:specgen> render --from-ir "${expected}" -o
                "${CMAKE_CURRENT_BINARY_DIR}/${case}.roundtrip.tex"
        )
        # The `.singlepass` sibling: `generate <header>` renders through the
        # backends without the IR ever becoming a file, so its wording must be
        # the bytes `render --from-ir <expected.json>` produces. There is no
        # third golden to keep -- the comparison is against this run's render of
        # the checked-in IR, which golden.<case> already pins.
        set(single_pass_args
            "-DSPECGEN=$<TARGET_FILE:specgen>"
            "-DHEADER=${ARG_INPUT}"
            "-DIR=${expected}"
            "-DACTUAL=${CMAKE_CURRENT_BINARY_DIR}/${case}.singlepass"
        )
        if(ARG_EXTRA_ARGS)
            list(APPEND single_pass_args "-DEXTRA_ARGS=${ARG_EXTRA_ARGS}")
        endif()
        if(ARG_COMPILE_COMMANDS_DIR)
            list(
                APPEND single_pass_args
                "-DCOMPILE_COMMANDS_DIR=${ARG_COMPILE_COMMANDS_DIR}"
            )
        endif()
        add_test(
            NAME golden.${case}.singlepass
            COMMAND
                ${CMAKE_COMMAND} ${single_pass_args} -P
                "${CMAKE_CURRENT_SOURCE_DIR}/run-single-pass.cmake"
        )
        if(NOT ARG_NO_VALIDATE)
            add_test(
                NAME golden.${case}.validate
                COMMAND
                    $<TARGET_FILE:specgen> render --from-ir "${expected}"
                    --validate -o
                    "${CMAKE_CURRENT_BINARY_DIR}/${case}.validate.tex"
            )
        endif()
    endif()

    list(
        APPEND GOLDEN_UPDATE_COMMANDS
        COMMAND
        ${CMAKE_COMMAND}
        ${case_args}
        -DUPDATE_GOLDEN=ON
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/run-golden.cmake"
    )
    set(GOLDEN_UPDATE_COMMANDS "${GOLDEN_UPDATE_COMMANDS}" PARENT_SCOPE)
endfunction()

# Render-mode cases: checked-in IR in, checked-in wording out. Registered on
# every lane.
specgen_add_golden(
    value_or
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/value_or/input.json"
)
specgen_add_golden(
    converting_ctor
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/converting_ctor/input.json"
)

# The same two inputs through the mpark/wg21 backend. Rendering the
# *identical* IR to a second target is the point — a difference between
# expected.tex and expected.md is a backend difference by construction, since
# nothing else in the pipeline moved.
#
# `value_or` covers the itemdecl fence, a sentence-form Mandates, the
# "Equivalent to:" block, and the index entry this backend drops (design §8).
# `converting_ctor` covers the rest of the vocabulary: an itemize as a nested
# `[#.#]{.pnum}` sublist, a `\seebelow` span inside a code fence, and a
# RefInline in prose.
specgen_add_golden(
    value_or
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/value_or/input.json"
)
specgen_add_golden(
    converting_ctor
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/converting_ctor/input.json"
)

# The same two inputs again, through the org backend. Three renderings of
# one IR is what makes a difference between the three expected files a backend
# difference by construction -- and for this pair of backends it also pins the
# *agreement* that matters: the bytes inside an org `#+begin_itemdecl` and
# inside the draft's `\begin{itemdecl}` are the same bytes, because org exports
# the one to the other. Compare `converting_ctor/expected.org`'s `@\seebelow@`
# against `expected.tex`'s.
specgen_add_golden(
    value_or
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/value_or/input.json"
)
specgen_add_golden(
    converting_ctor
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/converting_ctor/input.json"
)

# Paper mode. Its own fixture rather than a second rendering of one of
# the two above, because what it has to show is *sequence* -- the added-paragraph
# literals run x, x+1, x+2 within a wording div and restart in the next one, and
# a sublist takes its lead-in paragraph's literal. That needs several numbered
# paragraphs across two sections, which neither single-item case has. It also
# pins the two nodes that paper mode must leave alone: a top-level synopsis takes
# no wording div, so it takes no numbering either.
specgen_add_golden(
    paper_mode
    BACKEND mpark
    PAPER
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/paper_mode/input.json"
)

# Validate-mode case: a nested section (so the context prefix
# composes), one well-formed CodeText, and one overlapping-span table.
# Needs no Clang, so — like the render-mode cases above — it registers on
# every lane.
specgen_add_golden(
    validate_spans
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_spans/input.json"
)

# Validate-mode case: design §9's coverage invariant, both directions,
# over a hand-written roster — an unaccounted-for declaration, a description
# routed to a section that does not exist, and one routed to nothing, against
# a full set of dispositions that must stay silent. Hand-written IR rather
# than a corpus header precisely so it needs no Clang: the front end records
# what it saw, and the rule that judges it is Tier A's.
specgen_add_golden(
    validate_coverage
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_coverage/input.json"
)

# Validate-mode case: the empty-synopsis backstop. A Synopsis whose code
# is empty renders as an empty fenced code block, so it is an Error; the
# front end no longer produces one (a record declaration that defines
# nothing becomes an itemdecl or no node at all), which is exactly why the
# check is pinned over hand-written IR — the only channel that can still
# reach it, and the one a front-end regression would reopen.
specgen_add_golden(
    validate_empty_synopsis
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_empty_synopsis/input.json"
)

# Validate-mode case: design §9's leakage checker, over the same
# hand-written roster channel — each invisible disposition named in a
# different kind of wording fragment, against the visible ones named
# alongside them, which must stay silent. Hand-written IR for the same reason
# validate_coverage is: the rule is Tier A's, so its negative test needs no
# Clang.
specgen_add_golden(
    validate_leakage
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_leakage/input.json"
)

# Validate-mode case: the other half of the same checker -- a
# namespace qualifier that survived the front end's reference-resolved
# mapping, named in a synopsis, an itemdecl and an extracted body, with the
# roster half reporting beside it in the third. Hand-written like its
# siblings: what the front end contributes here is the `foreign` list, and a
# list is as easy to write as to compute.
specgen_add_golden(
    validate_qualifier
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_qualifier/input.json"
)

# Validate-mode case: design §5.2's Mandates/Constraints drift check --
# an authored element and its derived twin, over the same hand-written-IR
# channel validate_coverage/validate_leakage use. Hand-written for the same
# reason: the rule is Tier A's, so its negative test needs no Clang.
specgen_add_golden(
    validate_mandates_drift
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_mandates_drift/input.json"
)

# Validate-mode case: design §5.4's noexcept ↔ *Throws:* cross-check --
# a contradicting authored `\throws` and a redundant `Nothing` one, against
# the six readings of an exception specification that must stay silent.
# Hand-written IR like its three siblings above: the signature text is all
# this rule reads, so it needs no Clang either.
#
# EXPECT_EXIT 0 because both of this rule's findings are Warnings, which is
# itself the thing under test: an Error would make `--validate` skip rendering
# and answer a wrong *Throws:* paragraph with no wording at all. Pinning the
# exit code is what would catch a later promotion to Error. (The sibling above
# needed no such argument only because it borrows an unrelated Error from its
# roster to reach exit 1; it could shed that now.)
specgen_add_golden(
    validate_noexcept_throws
    MODE validate
    EXPECT_EXIT 0
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_noexcept_throws/input.json"
)

# Validate-mode case: design §6's unmarked-private-data nudge, read off
# the roster's `kind` field. The positive is a class that marks one
# data member `\expos` and leaves a sibling unmarked; the controls include the
# shape the *literal* reading of §6 would have reported and the corpus does
# not want reported (a class with private data and no `\expos` at all).
# EXPECT_EXIT 0: the nudge is a Note, so `--validate` renders as usual.
specgen_add_golden(
    validate_private_data
    MODE validate
    EXPECT_EXIT 0
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_private_data/input.json"
)

# Validate-mode case: the last of design §9's leakage clauses, at its
# note severity -- an undocumented helper named only by a body the tool never
# renders. Hand-written IR like its validate siblings: the front end's
# contribution is the `body_uses` list, and the rule that judges it is Tier A's.
#
# No EXPECT_EXIT, so the validate default of 1 applies, and it is earned by the
# fixture's one *leakage* Error rather than by anything this rule reports --
# which is itself under test, since that Error's name is also in `body_uses`
# and must draw no second finding (§9's "only").
specgen_add_golden(
    validate_unextracted_use
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_unextracted_use/input.json"
)

# Validate-mode case: a synopsis `\ref` group header naming a stable
# name that is not a section in the document. Hand-written like every other
# validate case above: the payload a SpanKind::Ref span carries is already
# in the IR, so this rule needs nothing new from the front end and
# runs on every lane. Two synopses, one clean (the negative control) and one
# carrying a Ref span that resolves beside one that does not.
specgen_add_golden(
    validate_dangling_ref
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/validate_dangling_ref/input.json"
)

# Generate-mode cases: header in, IR JSON out via `generate --emit-ir`.
# These need the driver, so like every other case here they are registered only
# where it is built.
specgen_add_golden(
    widget_skeleton
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_widget.hpp"
)

# Subtractive synopsis extraction (design §3.4).
specgen_add_golden(
    synopsis_extract
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_synopsis.hpp"
)

# A terminal verbatim-synopsis payload becomes one anonymous
# ordinary Synopsis without being parsed as C++ or description prose.
specgen_add_golden(
    verbatim_synopsis
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_verbatim.hpp"
)

# One standalone terminal exact-text payload becomes one itemdecl
# signature, retaining the authored description that precedes it.
specgen_add_golden(
    verbatim_itemdecl_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_verbatim_itemdecl.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/verbatim_itemdecl"
)

# A real specialization represented by a standalone exact-text
# item can suppress its implementation synopsis and derived class wording.
specgen_add_golden(
    verbatim_record_merge
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_verbatim_record_merge.hpp"
)

# A verbatim docblock *attached* to its declaration replaces the extracted
# form — one node, the authored one, never both (the duplication was the
# authored copy from the comment event plus the parsed copy from the decl
# event). `\verbatim-synopsis` keeps the class's members, roster, and
# routing; `\verbatim-itemdecl` masks the implementation spelling the
# extraction would have leaked.
specgen_add_golden(
    verbatim_attached
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_verbatim_attached.hpp"
)

# Clang declaration kinds derive standalone item indexes while
# synopsis-only facilities carry LibraryIndex spans on their names.
specgen_add_golden(
    index
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_index.hpp"
)

# Declarations bounded by the header synopsis's ordinary .syn
# section and exact Doxygen END fence gather into one Synopsis node.
specgen_add_golden(
    header_synopsis
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_header_synopsis.hpp"
)

# Normalization (design §3.6) — a class template exercising the
# draft FormatStyle's template/requires/concept options.
specgen_add_golden(
    template_synopsis
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_template.hpp"
)

# Redeclaration-chain attachment (design §3.3) — overloaded
# out-of-line member definitions, each with its own itemdecl/itemdescr.
specgen_add_golden(
    attach
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_attach.hpp"
)

# Documented namespace free-function definitions attach as ordinary
# items; unannotated implementation helpers remain absent.
specgen_add_golden(
    free_functions
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_free_functions.hpp"
)

# Structural markers (design §4.3) — `\also` overload grouping and
# `\omit`.
specgen_add_golden(
    markers
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_markers.hpp"
)

# Documented in-class aliases become routed itemdecls; adjacent
# aliases group with \also and implementation-detail RHS types are masked.
specgen_add_golden(
    aliases
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_aliases.hpp"
)

# A documented record declaration the header never defines is an ordinary
# itemdecl (an undefined class-template primary — never an empty code
# block); adjacent primaries group with \also, and a forward declaration of
# a class defined below, an undocumented never-defined declaration, and an
# `\omit`ted one all contribute nothing. The distinct case name leaves
# `undefined_primary` free for the backend renderings below; all consume one
# checked-in expected.json.
specgen_add_golden(
    undefined_primary_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_undefined_primary.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/undefined_primary"
)

# A class or class-template *definition*'s own docblock (design §6, issue
# #18): its description elements travel beside the synopsis as a
# description-only item, an authored `\mandates` replaces the derived
# class-scope paragraph and inherits its conjuncts, and a routed member's own
# item is unaffected. The distinct case name leaves `class_description` free
# for the backend renderings below; all consume one checked-in expected.json.
specgen_add_golden(
    class_description_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_class_description.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/class_description"
)

# Documented namespace-scope aliases, alias templates, variables, variable
# templates, and concepts are ordinary wording items; adjacent aliases group
# with \also, `\impdef` masks an alias RHS at namespace scope exactly as in
# a class body, and unannotated/`\omit`ted entities stay absent. The distinct
# case name leaves `namespace_entities` free for the backend renderings
# below; all consume one checked-in expected.json.
specgen_add_golden(
    namespace_entities_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_namespace_entities.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/namespace_entities"
)

# Constraints derivation (design §5.1) — a trailing requires-clause
# with a concept-id, two plain traits, and a parenthesized negation,
# derived into an itemize (past the sentence threshold) and stripped from
# the itemdecl.
specgen_add_golden(
    constraints
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_constraints.hpp"
)

# Authored `\item` entries lower into the same Itemize IR as
# derived conjuncts, while an authored `\iref{...}` becomes RefInline.
# The distinct case name leaves `authored_itemize` free for the default
# LaTeX rendering below; both consume one checked-in expected.json.
specgen_add_golden(
    authored_itemize_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_authored_itemize.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/authored_itemize"
)

# Authored two-dimensional tables lower from a real docblock and
# survive generate, JSON round-trip, and validation without findings.
specgen_add_golden(
    table2d_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_table2d.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/table2d"
)

# Mandates derivation (design §5.2) — a maximal static_assert prefix
# (one plain trait, then a top-level `&&` split into a trait and a negation)
# derived into an "and"-joined sentence; a later static_assert past a real
# statement is excluded from the prefix.
specgen_add_golden(
    mandates
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_mandates.hpp"
)

# A `\rSec` title clang-format wrapped across two plain `//` lines parses
# whole: the section opens with the joined title and its routed member
# validates. The corpus line is deliberately longer than the format column
# limit, so the formatter itself keeps the wrap this case exists to survive.
specgen_add_golden(
    rsec_wrapped
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_rsec_wrapped.hpp"
)

# Private-member elision leaves neither a stray leading `public:` (a
# struct's default access is already public) nor a silent invisible-name
# use: the surviving `using type = raw;` names the elided private alias,
# and the leak is the fixture. NO_VALIDATE for the same reason
# namespace_map takes it; `private_alias_validate` (below the Clang gate)
# pins the finding and its fixit.
specgen_add_golden(
    private_alias
    MODE generate
    NO_VALIDATE
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_private_alias.hpp"
)

# An in-class body is never synopsis content, whether or not it names an
# entity the drop set rewrites — the qualifier edit inside the body used to
# suppress the body splice through the overlap watermark, keeping the
# implementation in the synopsis for exactly the members that mention `std::`.
specgen_add_golden(
    inclass_bodies
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_inclass_bodies.hpp"
)

# A requires-expression in a template-head constraint is an ordinary
# constraint spelling (issue #20): clang anchors doc-comment attachment for a
# templated function at its name and rejects the `;{}#@` a requires-expression
# puts in between, so the docblock lookup must go through the described
# template. Each requires-expression form — in-class member, hidden friend,
# free function — must render exactly as its named-concept twin does. The
# distinct case name leaves `reqexpr` free for the backend renderings below;
# all consume one checked-in expected.json.
specgen_add_golden(
    reqexpr_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_reqexpr.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/reqexpr"
)

# A constructor's mem-initializer list is implementation, never interface
# (issue #21): the in-class-body splice starts at the `:` introducing a
# written ctor-initializer list, so the declaration alone renders in the
# synopsis and the itemdecl — no private spelling, no dropped-qualifier
# rewrite of the list. The distinct case name leaves `meminit` free for the
# backend renderings below; all consume one checked-in expected.json.
specgen_add_golden(
    meminit_generate
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_meminit.hpp"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/meminit"
)

# In-class-member attachment (design §3.3 sub-case, §6) — an
# out-of-line member, an in-class-defined member, and a hidden friend under
# one section, their itemdescrs interleaved in class-body order.
specgen_add_golden(
    inclass
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_inclass.hpp"
)

# In-class-member structural markers (design §4.3) — `\describe`
# forces an itemdecl for a `= default` member, `\merge` drops a defaulted
# twin from the synopsis, and `\at` routes an in-class member's itemdescr to
# an explicit section.
specgen_add_golden(
    inclass_markers
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_inclass_markers.hpp"
)

# Exposition-only members (design §3.5/§4.3) — `\expos` and
# `\expos(name)` data members render with an `\exposid` span (via the §3.6
# sentinel pipeline) and a `// exposition only` comment in the synopsis.
specgen_add_golden(
    expos
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_expos.hpp"
)

# `\seebelow` (design §4.3) — a deduced return type renders as a
# `\seebelow` span in both the synopsis and the itemdecl, with a `\remarks`
# stating the deduced type.
specgen_add_golden(
    seebelow
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_seebelow.hpp"
)

# Reference-resolved namespace mapping (design §3.5) — qualifiers
# resolving to `std` or to the header's own namespace are dropped from the
# synopsis and itemdecl; `detail::` is left verbatim for the
# leakage checker.
#
# NO_VALIDATE because "left verbatim" is itself a reported leak,
# and this header keeps the leak on purpose — it is the fixture for a
# qualifier surviving the mapping at all. `namespace_map_validate`, below
# the Clang gate, is where its finding is pinned.
specgen_add_golden(
    namespace_map
    MODE generate
    NO_VALIDATE
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_namespace.hpp"
)

# The qualifier half of the leakage checker sees through includes: the
# `detail::` machinery lives in support/spec_foreign_detail.hpp, not the
# main file, and its surviving qualifier is still recorded as foreign — the
# main-file boundary used to disable the check for exactly this shape. The
# std::ranges stand-in in the same include is the negative control: its
# `ranges::` qualifier renders and stays silent.
#
# NO_VALIDATE for the same reason namespace_map takes it: the leak is the
# fixture, and `foreign_include_validate` (below the Clang gate) pins the
# findings it draws.
specgen_add_golden(
    foreign_include
    MODE generate
    NO_VALIDATE
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_foreign_include.hpp"
)

# Exposition-only *uses* (design §3.5/§4.1) — an expos member
# referenced inside the class's own fragments renders as `\exposid`: in an
# extracted `*-equiv` body and in backticked prose.
specgen_add_golden(
    expos_uses
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_expos_uses.hpp"
)

# The design §10 acid test: an optional-shaped header —
# the golden trio (emplace, value_or, a converting constructor) plus
# `\merge`d defaulted twins, exposition-only storage, omitted private
# helpers, a hidden friend, and a `\seebelow` deduced return type.
# The destructor's `\ref` group header is removed when its sole member
# is `\merge`d away, so the ordinary generated-document validation runs.
specgen_add_golden(
    optional
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_optional.hpp"
)

# `\effects-equiv` / `\returns-equiv` body extraction (design §4.2) —
# a single-return body extracted into a Returns "Equivalent to:" block, and
# a body whose maximal static_assert prefix derives into Mandates while the
# remaining statements extract into an Effects "Equivalent to:" block.
specgen_add_golden(
    equiv
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_equiv.hpp"
)

# Preprocessing selects one branch before the AST is built. The
# extracted body keeps that active source but no inactive text or orphaned
# conditional directives.
specgen_add_golden(
    conditional_equiv
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_conditional_equiv.hpp"
)

# Include-path leg 1 (the `--` tail): `spec_include.hpp` reaches for a sibling
# header specgen cannot find without an `-I`. It lives under its own
# `consumer/` directory rather than directly beside `specgen_acid/` (see
# `specgen_acid/widget_traits.hpp`'s own comment) — a header reachable via
# a path relative to the *including* file's own directory resolves even
# through a plain angle include, because Clang's recovery for a failed
# `<angle>` search silently retries the same spelling as a `"quoted"`
# search relative to the includer before giving up. With `consumer/` in
# between, the failure is a genuine `fatal error`, and the golden is
# provably load-bearing: the IR generated without the `-I` differs from
# `expected.json` (an unlinked out-of-line definition renders with its
# `gadget::` qualification and an `undocumented` roster entry, rather than
# the clean in-class itemdecl `expected.json` pins), not merely absent.
set(include_path_header
    "${PROJECT_SOURCE_DIR}/tests/corpus/include_path/consumer/spec_include.hpp"
)
set(include_path_i_dir "${PROJECT_SOURCE_DIR}/tests/corpus/include_path")
specgen_add_golden(
    include_path
    MODE generate
    INPUT "${include_path_header}"
    EXTRA_ARGS "-I ${include_path_i_dir}"
)

# Leg 2 (a compile_commands.json): the same header, the same `-I`,
# supplied through a database instead of a `--` tail — generated into the
# build tree, never checked in (decision hermetic-corpus), with one
# entry naming this fixture's exact flags, plus a `-c`/`-o` pair to prove
# those are dropped by the same extraction that keeps the `-I`.
# EXPECTED_DIR points back at leg 1's own `expected.json`, which is the
# strongest form of "these are the same parse": one checked-in IR, two
# ways in, rather than two golden files that merely happen to agree.
set(include_path_db_dir "${CMAKE_CURRENT_BINARY_DIR}/include_path_db")
file(
    GENERATE OUTPUT "${include_path_db_dir}/compile_commands.json"
    CONTENT
        "[
  {
\"directory\": \"${include_path_db_dir}\",
\"file\": \"${include_path_header}\",
\"arguments\": [
  \"c++\",
  \"-I\", \"${include_path_i_dir}\",
  \"-c\",
  \"-o\", \"spec_include.o\",
  \"${include_path_header}\"
]
  }
]
"
)
specgen_add_golden(
    include_path_db
    MODE generate
    INPUT "${include_path_header}"
    COMPILE_COMMANDS_DIR "${include_path_db_dir}"
    EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include_path"
)

# The same fixture, its include deliberately left unsatisfied (no
# EXTRA_ARGS/COMPILE_COMMANDS_DIR this time) — `generate` must fail loudly
# on a header Clang could not fully parse rather than emit the
# plausible-looking wrong IR `golden.include_path`'s own comment describes
# above (the `gadget::`-qualified, "undocumented" itemdecl). EXPECT_EXIT 1
# is the assertion; the pinned stderr carries Clang's own diagnostic, caret
# diagram included, ahead of specgen's own message.
#
# That first line is coupled to the pinned LLVM version (decision
# llvm-toolchain-pin: LLVM 22) — a Clang upgrade that reworded this
# fatal error moves this golden, and
# that is expected, not a regression to chase. It is deliberately not
# filtered out: a user debugging a header wants Clang's own diagnostic,
# and hiding it to keep this golden stable would trade the tool's
# usefulness for the test's convenience.
specgen_add_golden(
    include_path_parse_error
    MODE diagnose
    INPUT "${include_path_header}"
    EXPECT_EXIT 1
)

# The build-time findings themselves, as golden text. Every other
# generate-mode case above compares the IR and lets stderr fall on the
# floor. `spec_diagnostics.hpp` is the one corpus header written to be
# reported on (its own top comment explains each case), so it is the one
# whose stderr is worth pinning.
#
# No EXPECT_EXIT: `diagnose` inherits the default 0, and that default is
# the assertion — the fixture contains an unknown-tag *Error*, and a build
# that stops emitting IR because one docblock has a typo would be worse
# than one that emits it with the finding beside it (the same call
# `render --validate` makes).
specgen_add_golden(
    diagnostics
    MODE diagnose
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_diagnostics.hpp"
)

# The documented-but-unsupported backstop: a docblock on an entity kind that
# produces no wording is an Error rather than a silent drop (a documented
# enum; a documented function declaration, whose markup belongs at the
# definition), while `\omit` and plain absence of markup stay quiet.
specgen_add_golden(
    unsupported_kinds
    MODE diagnose
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_unsupported_kinds.hpp"
)

# `///` and `/** */` are Doxygen's, and specgen reads neither.
# `spec_doxygen.hpp` puts both at every position a real header does; the
# IR is where the two halves of the gate are visible at once. Nothing
# Doxygen wrote reaches an itemdescr (it is not markup) and nothing
# Doxygen wrote reaches `synopsis.code.text` (it is not draft-form
# either), so the fixture's only wording is what its `//!` blocks say.
#
# Which is `spec_widget.hpp`'s wording, deliberately — the fixture is that
# header's class with Doxygen added, so the gate is not just "the output
# is clean" but "the output is the *same output*, byte for byte modulo the
# names". Compare `widget_skeleton/expected.json` beside this one; the
# only differences are `widget` for `gadget` and `\ref{widget.observers}`
# for `\ref{gadget.observers}`.
specgen_add_golden(
    doxygen
    MODE generate
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_doxygen.hpp"
)

# The second Doxygen gate, and a different kind of gate: `spec_doxygen_only.hpp`
# is documented entirely in Doxygen, with no specgen markup anywhere,
# which is how a library that has never heard of specgen is documented.
# The property under test is that pointing the tool at one **works** —
# parses, exits 0, emits IR that reads back and renders — not that the
# wording is any good, because there is no wording. Registering it as a
# generate case is the whole assertion: a crash, a nonzero exit or an
# unreadable IR each fail it, here and at the `.roundtrip` sibling.
#
# NO_VALIDATE, with `doxygen_only_validate` below pinning what
# `--validate` says instead — the convention `spec_namespace.hpp` also
# uses. What it says is one coverage Error per declaration, which is the
# honest answer for a header that describes nothing, and the failure mode
# it rules out is the tool quietly promoting `@brief` prose into an
# Effects paragraph.
specgen_add_golden(
    doxygen_only
    MODE generate
    NO_VALIDATE
    INPUT "${PROJECT_SOURCE_DIR}/tests/corpus/spec_doxygen_only.hpp"
)

# Exact draft markup in a verbatim itemdecl remains raw code text in
# every backend. The escape hatch supplies draft-form bytes, not semantic spans.
specgen_add_golden(
    verbatim_itemdecl
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/verbatim_itemdecl/expected.json"
)
specgen_add_golden(
    verbatim_itemdecl
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/verbatim_itemdecl/expected.json"
)
specgen_add_golden(
    verbatim_itemdecl
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/verbatim_itemdecl/expected.json"
)

# The authored-itemize IR generated above, through every backend. Registered
# after the Clang gate so wholesale golden regeneration writes expected.json
# before any renderer reads it. LaTeX emits an itemize and \iref; mpark and org
# emit their native list and cross-reference spellings from the identical
# semantic input.
specgen_add_golden(
    authored_itemize
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/authored_itemize/expected.json"
)

# The generated table IR rendered through every backend.
specgen_add_golden(
    table2d
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/table2d/expected.json"
)
specgen_add_golden(
    table2d
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/table2d/expected.json"
)
specgen_add_golden(
    table2d
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/table2d/expected.json"
)
specgen_add_golden(
    authored_itemize
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/authored_itemize/expected.json"
)
specgen_add_golden(
    authored_itemize
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/authored_itemize/expected.json"
)

# The undefined-primary IR rendered through every backend: the declaration
# itself as the itemdecl block, the draft's template-head line break kept,
# and no empty code block anywhere.
specgen_add_golden(
    undefined_primary
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/undefined_primary/expected.json"
)
specgen_add_golden(
    undefined_primary
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/undefined_primary/expected.json"
)
specgen_add_golden(
    undefined_primary
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/undefined_primary/expected.json"
)

# The class-description IR rendered through every backend: a class's own
# wording is a description with no itemdecl, so each backend emits its
# description blocks with no code block, itemdecl environment or itemdecl
# drawer around nothing.
specgen_add_golden(
    class_description
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/class_description/expected.json"
)
specgen_add_golden(
    class_description
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/class_description/expected.json"
)
specgen_add_golden(
    class_description
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/class_description/expected.json"
)

# The namespace-entity IR rendered through every backend: aliases, variable
# templates, and concepts as itemdecls, with the draft FormatStyle deciding
# the line shape (short alias and variable templates one-lined, concepts
# broken before `concept`).
specgen_add_golden(
    namespace_entities
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/namespace_entities/expected.json"
)
specgen_add_golden(
    namespace_entities
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/namespace_entities/expected.json"
)
specgen_add_golden(
    namespace_entities
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/namespace_entities/expected.json"
)

# The requires-expression IR rendered through every backend: the multi-line
# constraint shape survives the draft FormatStyle in synopsis and itemdecl
# alike, for members, hidden friends, and free functions.
specgen_add_golden(reqexpr INPUT "${CMAKE_CURRENT_SOURCE_DIR}/reqexpr/expected.json")
specgen_add_golden(
    reqexpr
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/reqexpr/expected.json"
)
specgen_add_golden(
    reqexpr
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/reqexpr/expected.json"
)

# The mem-initializer IR rendered through every backend: constructors whose
# in-class definitions carried initializer lists render as bare declarations
# in synopsis and itemdecl alike.
specgen_add_golden(meminit INPUT "${CMAKE_CURRENT_SOURCE_DIR}/meminit/expected.json")
specgen_add_golden(
    meminit
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/meminit/expected.json"
)
specgen_add_golden(
    meminit
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/meminit/expected.json"
)

# `spec_namespace.hpp`'s standing finding, read off its *own* generate
# golden. Needs no Clang — the input is the checked-in expected.json — so it
# runs on every lane, which is the point: the leakage rule is Tier A, and the
# corpus is only where the leak happens to live.
#
# Registered after the Clang-gated block on purpose. `make goldens` runs
# GOLDEN_UPDATE_COMMANDS in registration order, and this case reads the file
# `namespace_map` writes; ordered the other way, one regeneration would lag a
# run behind the header it describes.
specgen_add_golden(
    namespace_map_validate
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/namespace_map/expected.json"
)

# `spec_foreign_include.hpp`'s standing findings, read off its own generate
# golden the same way `namespace_map_validate` reads its header's — and
# registered after the Clang-gated block for the same regeneration-ordering
# reason. What the hand-written validate_qualifier sibling cannot pin: that
# the front end records a foreign namespace whose declaration lives in an
# included header at all.
specgen_add_golden(
    foreign_include_validate
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/foreign_include/expected.json"
)

# `spec_private_alias.hpp`'s standing finding, read off its own generate
# golden the same way `namespace_map_validate` reads its header's — and
# registered after the Clang-gated block for the same regeneration-ordering
# reason: the surviving `using type = raw;` names the elided private alias,
# the synopsis-side hidden-name check's one non-double-reporting case.
specgen_add_golden(
    private_alias_validate
    MODE validate
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/private_alias/expected.json"
)

# `spec_optional.hpp`'s standing note, read off its own generate golden
# the same way `namespace_map_validate` reads its header's -- and registered
# after the Clang-gated block for the same regeneration-ordering reason.
#
# The header's default constructor calls the undocumented `hard_reset()`, which
# is the design's own §9 example in the position where it is a note rather than
# an error (the body carries no `\effects-equiv`, so no wording names it). Two
# things are pinned that the hand-written sibling cannot pin: that the front
# end's `body_uses` collection reaches a real header end to end, and -- via
# EXPECT_EXIT 0 -- that a note-only document still renders.
# The empty destructor group is removed before it can become a dangling Ref,
# preserving this case's note-only, still-renders contract.
specgen_add_golden(
    optional_validate
    MODE validate
    EXPECT_EXIT 0
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/optional/expected.json"
)

# What `--validate` says about a header documented entirely in Doxygen,
# read off `doxygen_only`'s own generate golden the way the two cases above
# read theirs -- and registered after the Clang-gated block for the same
# regeneration-ordering reason.
#
# One coverage Error per declaration, and nothing else. Both halves matter.
# The Errors are the tool saying the header carries no wording, which is the
# truth about it and is *why* this case exists: the alternative failure mode
# is silence, and silence here would mean `@brief` prose had been promoted
# into wording somewhere. That nothing else is reported is the second half --
# no leakage finding, no dangling reference, no malformed marker, because a
# Doxygen block reaches none of those channels.
specgen_add_golden(
    doxygen_only_validate
    MODE validate
    EXPECT_EXIT 1
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/doxygen_only/expected.json"
)

# The acid-test header rendered to mpark markdown, read off its own
# generate golden the way the two validate cases above read theirs — so it runs
# on every lane, and is registered after the Clang-gated block for the same
# regeneration-ordering reason.
#
# The two hand-written mpark cases at the top of this file are single items;
# this is the one that exercises a *document*: `\rSec` headings at two depths,
# several class synopses (which take no `::: wording` div), exposition-only
# spans in extracted bodies and prose, and the interleaved wording of a real
# class. It is also the artifact to feed the wg21 pandoc pipeline when checking
# this backend against the framework out of tree (decision hermetic-corpus).
specgen_add_golden(
    optional
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/optional/expected.json"
)

# And the same header through the org backend, for the reason its mpark
# twin above exists -- this is the one org case that exercises a *document*
# rather than an item: headings at two depths carrying stable names, several
# class synopses, `\ref` spans in synopsis group comments (bare, per
# `texcl=true`), and exposition-only spans in both an extracted body and prose,
# where the two render differently on purpose (`@\exposid{value}@` in a
# codeblock, `@@latex:\exposid{value}@@` out in org prose).
specgen_add_golden(
    optional
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/optional/expected.json"
)

# The one golden that exists for this backend alone. Every other backend
# renders a span the same way wherever it lands; org does not, because `@...@`
# is a listings option and is inert outside the environments that set it. So an
# exposition-only member reads `@\exposid{value}@` in a codeblock and
# `@@latex:\exposid{value}@@` out in prose, and `spec_expos_uses.hpp` is the
# corpus header that puts both in one fragment -- an extracted `*-equiv` body
# and a backticked `\remarks`. Read off its own generate golden, after the
# Clang gate, for the regeneration-ordering reason above.
specgen_add_golden(
    expos_uses
    BACKEND org
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/expos_uses/expected.json"
)

# The fragment-path scheme, over the same acid-test header the three
# whole-document cases above render — read off its own generate golden, and
# registered after the Clang gate for the regeneration-ordering reason those
# share. One case per backend, because the extension is the one part of the
# scheme that is a backend fact; the six stems are identical across all three,
# which is the property that says the split is not a backend's.
#
# What each run pins beyond the paths: the fragments are byte-for-byte what the
# whole-document render produces for those nodes, the manifest carries document
# order (which the directory listing loses), and running twice into one
# directory changes nothing — design §8's "wholesale and idempotent", which is
# what these three cases gate.
#
# The latex case is also, incidentally, the only `.tex` golden over a
# *generated* document: no other generate-mode case renders to
# LaTeX, so the bare `\ref{optional.ctor}` in a synopsis group comment
# — the draft's `texcl=true` exception — is watched here in
# `expected.latex/optional.tex`, and otherwise by a unit test alone.
specgen_add_golden(
    fragments
    MODE split
    BACKEND latex
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/optional/expected.json"
)
specgen_add_golden(
    fragments
    MODE split
    BACKEND mpark
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/optional/expected.json"
)

# The same split with an explicit `--root`, which is the other half of the
# root-fragment rule: `optional.syn.org` here against the derived `optional`
# above, from one input and one code path.
specgen_add_golden(
    fragments
    MODE split
    BACKEND org
    ROOT optional.syn
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/optional/expected.json"
)

# Regeneration is wholesale and lands in the diff, never silent.
add_custom_target(
    goldens
    ${GOLDEN_UPDATE_COMMANDS}
    DEPENDS specgen
    COMMENT "Regenerating golden files"
    VERBATIM
)
