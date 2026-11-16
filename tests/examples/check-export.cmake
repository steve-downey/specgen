# tests/examples/check-export.cmake                                -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# docs/examples.md is the checked-in GFM export of docs/examples.org, and it
# embeds every file the document transcludes: the scripts, their inputs, and
# the captured outputs. Exporting needs Emacs (`make examples-md`), so the
# export is not rebuilt by the test suite, and it fell several releases behind
# the captures it shows without anything noticing (issue #294).
#
# This is the check that needs no Emacs: every transcluded file's text must
# appear in the export, byte for byte. A capture that changed without a
# re-export fails here, naming the file and the command to run.
#
# It reads the transclusions as `:src` blocks of whole files, which is what
# every one in the document is. A ranged one (`:lines`, `:end`) embeds part of
# a file and would need this script to learn the range.
file(READ "${ORG}" org)
file(READ "${EXPORT}" exported)
get_filename_component(org_dir "${ORG}" DIRECTORY)

string(
    REGEX MATCHALL "#\\+transclude: \\[\\[file:[^]]+\\]\\][^\n]*"
    lines
    "${org}"
)
list(LENGTH lines count)
if(count EQUAL 0)
    message(FATAL_ERROR "no transclusions found in ${ORG}")
endif()

set(stale "")
foreach(line IN LISTS lines)
    if(line MATCHES ":lines|:end")
        message(FATAL_ERROR "a ranged transclusion is not checked: ${line}")
    endif()
    string(
        REGEX REPLACE "^#\\+transclude: \\[\\[file:([^]]+)\\]\\].*$"
        "\\1"
        relative
        "${line}"
    )
    file(READ "${org_dir}/${relative}" text)
    string(REGEX REPLACE "\n+$" "" text "${text}")
    string(FIND "${exported}" "${text}" at)
    if(at LESS 0)
        string(APPEND stale "  ${relative}\n")
    endif()
endforeach()

if(stale)
    message(
        FATAL_ERROR
        "docs/examples.md does not embed the current text of:\n${stale}"
        "Re-export it with `make examples-md` (needs Emacs) and commit the result."
    )
endif()
message(STATUS "docs/examples.md embeds all ${count} transcluded files")
