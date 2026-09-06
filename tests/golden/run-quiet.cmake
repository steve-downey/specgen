# tests/golden/run-quiet.cmake                                     -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The silence sibling of a generate-mode golden (issue #65). A golden compares
# the IR on stdout; this requires that the run said *nothing* on stderr.
#
# The corpus is meant to be well-formed markup, and a header that is not says
# so through a finding. Until this existed, a finding from an ordinary corpus
# header went nowhere: only a `MODE diagnose` case looks at stderr, and the
# headers that have one are the ones written to be malformed. So a typo in a
# working header -- `\expos widget-like` for `\expos(widget-like)`, whose
# derived name happened to match what the author meant -- warned on every run
# for as long as nobody read the scrollback.
#
# This is the mirror of `golden.<case>.validate`, which demands the same
# silence from the *validator* unless a case opts out with NO_VALIDATE. There
# is deliberately no opt-out here: a header that should carry a finding gets a
# `MODE diagnose` case, which pins the exact text and is the stronger gate
# AGENTS.md already asks for ("pin the diagnostic, never just skip a case").

if(NOT SPECGEN OR NOT INPUT OR NOT ACTUAL)
    message(
        FATAL_ERROR
        "run-quiet.cmake: SPECGEN, INPUT, and ACTUAL are required"
    )
endif()

set(tail_args "")
if(EXTRA_ARGS)
    separate_arguments(tail_args NATIVE_COMMAND "${EXTRA_ARGS}")
endif()

execute_process(
    COMMAND
        "${SPECGEN}" generate --emit-ir "${INPUT}" --no-compile-commands -o
        "${ACTUAL}" -- ${tail_args}
    RESULT_VARIABLE result
    ERROR_VARIABLE findings
)

# A nonzero *exit code* is the golden's own business -- `generate` reports
# findings and still emits IR, so it is not what says whether the run was
# quiet, and duplicating the golden's check here would report it twice. A
# failure to run at all is a different thing, and has to be caught here or this
# case passes on an empty stderr it never got: execute_process reports that as
# a message rather than a number, which is what distinguishes the two.
if(NOT result MATCHES "^[0-9]+$")
    message(FATAL_ERROR "run-quiet.cmake: could not run ${SPECGEN}: ${result}")
endif()

if(NOT findings STREQUAL "")
    get_filename_component(header_name "${INPUT}" NAME)
    message(
        FATAL_ERROR
        "${header_name} is a well-formed corpus header but `generate` reported:\n"
        "${findings}"
        "Fix the header, or -- if the finding is the point of the case -- give it a "
        "`MODE diagnose` golden, which pins the exact text."
    )
endif()
