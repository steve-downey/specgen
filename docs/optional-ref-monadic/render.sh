#!/bin/sh
# docs/optional-ref-monadic/render.sh                                 -*-sh-*-
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Typeset the files generate.sh wrote, each with the paper framework its
# backend is written for, into rendered/, which is committed so the pages
# can be opened on GitHub:
#
#   wording.pdf, diff.pdf               latex, with wg21-latex's draft macros
#   wording.mpark.html, diff.mpark.html mpark, with mpark/wg21
#   wording.org.html, diff.org.html     org, with wg21org
#
# Each framework is a checkout named by an environment variable; a framework
# whose variable is unset is skipped, with a note saying so.
#
#   WG21_LATEX  a wg21-latex checkout (the directory with common.tex)
#   MPARK_WG21  an mpark/wg21 checkout (the directory with flat.mk), with its
#               pandoc and Python dependencies already installed, as `make`
#               in a paper using it leaves them
#   WG21ORG     a wg21org checkout (the directory with ox-wg21html.el), with
#               its Emacs packages installed, as its own `make` leaves them
#   EMACS       the Emacs to run (default: emacs)
#
# What each framework typesets is the generated file as it is, behind a
# title, with two exceptions. Each works around a defect in specgen's output
# that stops that framework's build. The script says when it applies one, and
# each is to be removed when its issue is fixed.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=$HERE/output
RENDERED=$HERE/rendered
EMACS=${EMACS:-emacs}
TITLE_WORDING="[optional.ref.monadic]: generated wording"
TITLE_DIFF="[optional.ref.monadic]: generated wording, marked against the working draft"

WORK=$(mktemp -d)
# RENDER_KEEP=1 keeps the scratch directory, for reading a failed build's logs.
if [ -n "${RENDER_KEEP:-}" ]; then
    printf 'render.sh: keeping %s\n' "$WORK" >&2
else
    trap 'rm -rf "$WORK"' EXIT
fi
mkdir -p "$RENDERED"
built=0

note() { printf 'render.sh: %s\n' "$*" >&2; }

record() { # record <name> <checkout>
    printf '%s: %s\n' "$1" "$(git -C "$2" log -1 --format='%h %cs' -- . 2>/dev/null || echo unknown)" \
        >>"$RENDERED/frameworks.txt"
}
: >"$RENDERED/frameworks.txt"

# --- latex, with wg21-latex -------------------------------------------------

if [ -n "${WG21_LATEX:-}" ]; then
    mkdir -p "$WORK/latex"
    cp "$OUT/wording/latex/optional.ref.synopsis.tex" \
        "$OUT/wording/latex/optional.ref.monadic.tex" "$WORK/latex/"

    # Issue #410: a changed run of code is not escaped, and `&` inside one
    # stops the build. Escape `&` inside @\added{...}@ and @\removed{...}@.
    sed -e 's/\(@\\\(added\|removed\){[^@]*\)\([^\\]\)&/\1\3\\\&/g' \
        "$OUT/diff/latex/optional.ref.monadic.tex" >"$WORK/latex/diff-fragment.tex"
    if ! cmp -s "$OUT/diff/latex/optional.ref.monadic.tex" "$WORK/latex/diff-fragment.tex"; then
        note "diff.pdf: escaped & inside a changed run of code (specgen issue #410)"
    fi

    latex_doc() { # latex_doc <name> <title> <fragment>...
        name=$1 title=$2
        shift 2
        {
            printf '%s\n' '\documentclass[a4paper,10pt,oneside,openany,final,article]{memoir}' \
                '\input{common}' '\begin{document}' "\\chapter*{$title}" '\begin{wording}'
            for f in "$@"; do printf '\\input{%s}\n' "$f"; done
            printf '%s\n' '\end{wording}' '\end{document}'
        } >"$WORK/latex/$name.tex"
        (cd "$WORK/latex" &&
            TEXINPUTS=$WG21_LATEX//: latexmk -pdflua -interaction=nonstopmode -halt-on-error \
                "$name.tex" >"$name.console" 2>&1) || {
            note "$name.pdf failed; the end of the LaTeX log:"
            tail -20 "$WORK/latex/$name.log" >&2
            exit 1
        }
        cp "$WORK/latex/$name.pdf" "$RENDERED/$name.pdf"
    }
    latex_doc wording "$TITLE_WORDING" optional.ref.synopsis optional.ref.monadic
    latex_doc diff "$TITLE_DIFF" diff-fragment
    record wg21-latex "$WG21_LATEX"
    built=1
else
    note "WG21_LATEX is not set; skipping the PDFs"
fi

# --- mpark, with mpark/wg21 -------------------------------------------------

if [ -n "${MPARK_WG21:-}" ]; then
    mkdir -p "$WORK/mpark"
    mpark_doc() { # mpark_doc <name> <title> <fragment>...
        name=$1 title=$2
        shift 2
        {
            printf -- '---\ntitle: "%s"\ndocument: specgen example\ndate: today\naudience: LWG\n---\n\n' \
                "$title"
            for f in "$@"; do cat "$f"; echo; done
        } >"$WORK/mpark/$name.md"
    }
    mpark_doc wording "$TITLE_WORDING" \
        "$OUT/wording/mpark/optional.ref.synopsis.md" "$OUT/wording/mpark/optional.ref.monadic.md"
    mpark_doc diff "$TITLE_DIFF" "$OUT/diff/mpark/optional.ref.monadic.md"

    # Issue #406: an authored \codeblock can name an exposition-only member
    # only by the LaTeX escape @\exposid{x}@, which stops mpark/wg21's
    # filter. Write it as mpark's own $x$.
    for f in wording diff; do
        sed -e 's/@\\exposid{\([^}]*\)}@/$\1$/g' "$WORK/mpark/$f.md" >"$WORK/mpark/$f.md.tmp"
        if ! cmp -s "$WORK/mpark/$f.md" "$WORK/mpark/$f.md.tmp"; then
            note "$f.mpark.html: wrote @\\exposid{...}@ as \$...\$ (specgen issue #406)"
        fi
        mv "$WORK/mpark/$f.md.tmp" "$WORK/mpark/$f.md"
    done

    printf 'OUTDIR := .\ninclude %s/flat.mk\n' "$MPARK_WG21" >"$WORK/mpark/Makefile"
    for f in wording diff; do
        make -s -C "$WORK/mpark" "$f.html" >"$WORK/mpark/$f.console" 2>&1 || {
            note "$f.mpark.html failed:"
            tail -20 "$WORK/mpark/$f.console" >&2
            exit 1
        }
        cp "$WORK/mpark/$f.html" "$RENDERED/$f.mpark.html"
    done
    record mpark/wg21 "$MPARK_WG21"
    built=1
else
    note "MPARK_WG21 is not set; skipping the mpark HTML"
fi

# --- org, with wg21org ------------------------------------------------------

if [ -n "${WG21ORG:-}" ]; then
    mkdir -p "$WORK/org"
    org_doc() { # org_doc <name> <title> <fragment>...
        name=$1 title=$2
        shift 2
        {
            printf '#+title: %s\n#+DOCNUMBER: specgen example\n#+AUDIENCE: LWG\n' "$title"
            printf '#+options: toc:nil num:nil\n\n* Wording\n:PROPERTIES:\n:UNNUMBERED: t\n:END:\n\n'
            for f in "$@"; do printf '#+INCLUDE: "%s"\n\n' "$f"; done
        } >"$WORK/org/$name.org"
        (cd "$WG21ORG" &&
            WG21_BABEL=no "$EMACS" --batch --init-directory=emacs.d \
                --load emacs.d/export-init.el \
                --eval '(setq enable-local-variables :all)' \
                --load ox-wg21html.el \
                --visit "$WORK/org/$name.org" \
                -f my-wg21-export-to-html -f wg21org-exit) >"$WORK/org/$name.console" 2>&1 &&
            [ -s "$WORK/org/$name.html" ] || {
            note "$name.org.html failed:"
            tail -20 "$WORK/org/$name.console" >&2
            exit 1
        }
        cp "$WORK/org/$name.html" "$RENDERED/$name.org.html"
    }
    org_doc wording "$TITLE_WORDING" \
        "$OUT/wording/org/optional.ref.synopsis.org" "$OUT/wording/org/optional.ref.monadic.org"
    org_doc diff "$TITLE_DIFF" "$OUT/diff/org/optional.ref.monadic.org"
    record wg21org "$WG21ORG"
    built=1
else
    note "WG21ORG is not set; skipping the org HTML"
fi

[ "$built" = 1 ] || {
    note "nothing to do: set at least one of WG21_LATEX, MPARK_WG21, WG21ORG"
    exit 2
}
