# tools/check-raw-loops.cmake                                      -*-CMake-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# The "No raw loops" gate (docs/CODING_RULES.md). A `for`/`while`/`do` loop
# outside a substrate generic algorithm is a defect: either it is a named
# algorithm and should be converted (fold, transform/append, views::zip,
# find_if/any_of/none_of, join_with, ...), or it is a genuine primitive and
# must carry the exact marker comment `// substrate generic algorithm` plus a
# one-line reason, on the loop line itself or in the contiguous comment block
# immediately above it. This script finds every loop site that carries
# neither.
#
# Usage (run with `cmake -P`, not as a CMakeLists.txt include):
#
#   cmake -P tools/check-raw-loops.cmake
#   cmake -DPATHS="src/beman/specgen/lower.cpp;src/beman/specgen/ir.cpp" -P tools/check-raw-loops.cmake
#
# With no PATHS, the default roots (src/, include/, tools/) are scanned for
# *.cpp, *.hpp, *.cppm. With PATHS set, it is a `;`-separated list of files
# or directories -- relative to the repo root, or absolute -- and exactly
# those are scanned instead. Per-step spot checks use the PATHS form to
# narrow the gate to the files a step touched.
#
# Excluded from the default scan, and from directory expansion under PATHS:
#   - anything under vendor/     -- a git subtree; local edits are forbidden
#     (decision subtree-consumption).
#   - anything under tests/, and any *.test.cpp file -- out of this sweep's
#     scope; the no-raw-loops gate covers production files only.
#   - this script's own directory (tools/), other than *.cpp/*.hpp -- so the
#     gate does not go looking at its own CMake sources for loops.
# A path passed explicitly via PATHS is scanned even if it sits under one of
# these trees, since a spot check may deliberately want to, but directory
# expansion (the no-PATHS default, and any directory named in PATHS) still
# applies the exclusions.
#
# A line, after stripping leading whitespace, is a loop site if it matches:
#   - `for` then optional whitespace then `(`, not preceded by an identifier
#     character (so `xfor(` does not match);
#   - `while` then optional whitespace then `(`, same identifier-boundary
#     rule;
#   - `do` then optional whitespace then `{`.
# Two refinements:
#   - Comment lines are skipped entirely (first non-whitespace characters
#     `//`, `/*`, or `*`) -- this is what keeps prose like parser.hpp's
#     "...has no equivalent for (upstream matches..." from being counted.
#   - A `}` ... `while` ... `(` line is the closing half of a `do { ... }
#     while (...)` already counted at its `do {` line, and is skipped so the
#     one loop does not demand two markers.
#
# A loop site is marked if the literal text `substrate generic algorithm`
# appears either on the loop line itself (trailing-comment form, as
# foundation/monoid.hpp writes it) or anywhere in the contiguous run of
# comment lines immediately above the loop line (block form, as
# backend/common.hpp writes it) -- walking upward while each line's first
# non-whitespace is `//`, `/*`, or `*`, stopping at the first non-comment
# line. A marker separated from its loop by code marks nothing: a comment
# above a function that contains three loops marks none of them, since each
# loop must carry its own.
#
# For every unmarked loop site, prints one greppable, editor-clickable line:
#
#   <repo-relative-path>:<line>: unmarked raw loop: <the trimmed source line>
#
# then a summary with the marked and unmarked counts. Exits 0 if there are no
# unmarked sites, exits 1 (via message(FATAL_ERROR ...)) otherwise.
#
# This runs as the ctest case `style.no-raw-loops` (registered in the top-level
# CMakeLists.txt, `ctest -R style` selects it), unconditionally in both build
# configurations -- it reads source text, so it needs nothing built and gives
# the same answer in every configuration.
#
# Known limits (this is a line-oriented text scan, not a parser):
#   - A loop whose `(` sits on the *following* line -- `for` alone, then
#     `(init; cond; step)` -- is not detected, so it would slip through
#     unmarked. No such loop exists in the tree today (clang-format keeps the
#     `(` attached), which is why this is recorded rather than fixed; the fix
#     is a one-line lookahead when the current line is a bare `for`/`while`.
#   - A loop written inside a macro body, or produced by one, is invisible to
#     the scan. The tree defines no such macro.
#   - The marker is matched as literal text, so it is found in a *string
#     literal* too. Nothing in the tree writes one, and the failure direction
#     is a false pass on a line that had to go out of its way to spell it.

# Resolve the repo root from the script's own location, not the working
# directory, so this also works invoked from a build tree.
get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

set(_marker "substrate generic algorithm")

# ----------------------------------------------------------------------------
# Build the list of files to scan.
# ----------------------------------------------------------------------------

function(_is_excluded abs_path out_var)
    file(RELATIVE_PATH rel "${_repo_root}" "${abs_path}")
    set(excluded FALSE)
    if(rel MATCHES "^vendor/")
        set(excluded TRUE)
    elseif(rel MATCHES "^tests/")
        set(excluded TRUE)
    elseif(rel MATCHES "\\.test\\.cpp$")
        set(excluded TRUE)
    elseif(rel MATCHES "^tools/" AND NOT rel MATCHES "\\.(cpp|hpp)$")
        set(excluded TRUE)
    endif()
    set("${out_var}" "${excluded}" PARENT_SCOPE)
endfunction()

function(_collect_dir abs_dir out_var)
    set(found "")
    file(
        GLOB_RECURSE candidates
        "${abs_dir}/*.cpp"
        "${abs_dir}/*.hpp"
        "${abs_dir}/*.cppm"
    )
    foreach(candidate IN LISTS candidates)
        _is_excluded("${candidate}" skip)
        if(NOT skip)
            list(APPEND found "${candidate}")
        endif()
    endforeach()
    set("${out_var}" "${found}" PARENT_SCOPE)
endfunction()

set(_files "")

if(DEFINED PATHS)
    foreach(entry IN LISTS PATHS)
        if(IS_ABSOLUTE "${entry}")
            set(abs_entry "${entry}")
        else()
            set(abs_entry "${_repo_root}/${entry}")
        endif()
        if(IS_DIRECTORY "${abs_entry}")
            _collect_dir("${abs_entry}" dir_files)
            list(APPEND _files ${dir_files})
        elseif(EXISTS "${abs_entry}")
            list(APPEND _files "${abs_entry}")
        else()
            message(
                WARNING
                "check-raw-loops: PATHS entry does not exist: ${entry}"
            )
        endif()
    endforeach()
else()
    foreach(root src include tools)
        set(abs_root "${_repo_root}/${root}")
        if(IS_DIRECTORY "${abs_root}")
            _collect_dir("${abs_root}" dir_files)
            list(APPEND _files ${dir_files})
        endif()
    endforeach()
endif()

list(REMOVE_DUPLICATES _files)
list(SORT _files)

# ----------------------------------------------------------------------------
# Split one file's content into lines, without ever letting CMake's list
# machinery see the result.
#
# file(STRINGS ...) and the tempting file(READ) + string(REPLACE ";" "\n")
# + list(GET ...) combination both hand the joined text to CMake's *list*
# parser at some point (list(GET), list(LENGTH), foreach(IN LISTS ...)).
# That parser applies legacy bracket-argument rules to the value, and an
# unmatched '[' -- exactly what this codebase's half-open-range comments
# write, e.g. "[text_begin, text_begin + text.size())" -- makes it silently
# swallow the rest of the file into one element with no further line
# splitting. It is not a semicolon-escaping problem; it reproduces with a
# lone '[' and no semicolons at all. The fix is to never call a list()
# command, or foreach(IN LISTS ...), on the per-line text: walk the raw
# string with string(FIND)/string(SUBSTRING) and stash each line in its own
# scalar variable (_CRL_line_<n>) instead of a list element.
# ----------------------------------------------------------------------------

function(_split_into_lines content out_count_var)
    string(LENGTH "${content}" _len)
    set(_pos 0)
    set(_n 0)
    while(_pos LESS _len)
        math(EXPR _remaining "${_len} - ${_pos}")
        string(SUBSTRING "${content}" ${_pos} ${_remaining} _tail)
        string(FIND "${_tail}" "\n" _nl)
        if(_nl EQUAL -1)
            string(SUBSTRING "${content}" ${_pos} -1 _line)
            set(_pos ${_len})
        else()
            string(SUBSTRING "${content}" ${_pos} ${_nl} _line)
            math(EXPR _pos "${_pos} + ${_nl} + 1")
        endif()
        set("_CRL_line_${_n}" "${_line}" PARENT_SCOPE)
        math(EXPR _n "${_n} + 1")
    endwhile()
    set("${out_count_var}" "${_n}" PARENT_SCOPE)
endfunction()

# ----------------------------------------------------------------------------
# Scan each file.
# ----------------------------------------------------------------------------

set(_unmarked_count 0)
set(_marked_count 0)

foreach(abs_path IN LISTS _files)
    file(RELATIVE_PATH rel_path "${_repo_root}" "${abs_path}")

    file(READ "${abs_path}" _raw)
    _split_into_lines("${_raw}" _line_count)

    if(_line_count EQUAL 0)
        continue()
    endif()
    math(EXPR _last_index "${_line_count} - 1")

    foreach(idx RANGE 0 ${_last_index})
        set(_line "${_CRL_line_${idx}}")

        string(REGEX REPLACE "^[ \t]+" "" _trimmed "${_line}")

        if(_trimmed MATCHES "^(//|/\\*|\\*)")
            continue()
        endif()

        set(_is_loop FALSE)

        if(_trimmed MATCHES "^\\}[ \t]*while[ \t]*\\(")
            # Closing half of a do-while; the opening `do {` already counted.
            set(_is_loop FALSE)
        elseif(_trimmed MATCHES "(^|[^A-Za-z0-9_])for[ \t]*\\(")
            set(_is_loop TRUE)
        elseif(_trimmed MATCHES "(^|[^A-Za-z0-9_])while[ \t]*\\(")
            set(_is_loop TRUE)
        elseif(_trimmed MATCHES "(^|[^A-Za-z0-9_])do[ \t]*\\{")
            set(_is_loop TRUE)
        endif()

        if(NOT _is_loop)
            continue()
        endif()

        math(EXPR _line_no "${idx} + 1")

        # Marked on the loop line itself?
        set(_marked FALSE)
        if(_line MATCHES "${_marker}")
            set(_marked TRUE)
        else()
            # Walk upward through the contiguous comment block immediately
            # above this line, stopping at the first non-comment line.
            set(_walk_idx ${idx})
            while(_walk_idx GREATER 0)
                math(EXPR _walk_idx "${_walk_idx} - 1")
                set(_above "${_CRL_line_${_walk_idx}}")
                string(REGEX REPLACE "^[ \t]+" "" _above_trimmed "${_above}")
                if(NOT _above_trimmed MATCHES "^(//|/\\*|\\*)")
                    break()
                endif()
                if(_above MATCHES "${_marker}")
                    set(_marked TRUE)
                    break()
                endif()
            endwhile()
        endif()

        if(_marked)
            math(EXPR _marked_count "${_marked_count} + 1")
        else()
            math(EXPR _unmarked_count "${_unmarked_count} + 1")
            message("${rel_path}:${_line_no}: unmarked raw loop: ${_trimmed}")
        endif()
    endforeach()
endforeach()

message("check-raw-loops: ${_marked_count} marked, ${_unmarked_count} unmarked")

if(_unmarked_count GREATER 0)
    message(
        FATAL_ERROR
        "check-raw-loops: ${_unmarked_count} unmarked raw loop(s) found"
    )
endif()
