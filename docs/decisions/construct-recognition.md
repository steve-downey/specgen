# Recognizing a source construct: from the AST, or from its text

## Status

Accepted

## Context

Synopsis extraction is subtractive (design §3.4): the class's own source text
is lexed out of the main file and exactly two things are removed from it. A
removal is a byte range, so the front end holds source locations and asks
questions of them with the raw lexer — `Lexer::findNextToken` from a recorded
end location, one token at a time.

Two of those questions are about a `= delete` / `= default` tail. *Is one
there* — because Clang's own parser
([#85](https://github.com/steve-downey/specgen/issues/85)) extends a deleted
or defaulted function's recorded end past the keyword only when the
declarator Sema handed back is itself a `FunctionDecl`; for a member function
*template* it is the `FunctionTemplateDecl`, the internal cast fails
silently, and the end is left at the parameter list's closing `)`. And *how
far does it reach* — because even where the end is extended it stops at the
keyword, so P2573's `= delete("reason")` leaves the message past it.

Issue #85 answered both questions from the text: raw-lex forward, require a
bare `=`, then compare the next token's spelling against `"delete"` and
`"default"`. The spelling comparison is not a shortcut but a consequence of
raw lexing — `findNextToken` never runs the identifier-table pass, so both
keywords arrive as a plain `tok::raw_identifier` and nothing but their text
distinguishes them.

Text is not where the answer is. A tail written through a macro raw-lexes to
the macro's own identifier, so the test never fires and the #85 fragment
returns exactly as before
([#99](https://github.com/steve-downey/specgen/issues/99)). That is not a
contrived spelling: `bemanproject/expected` deletes two constructor templates
through `BEMAN_EXPECTED_DELETE_MSG(msg)`, expanding to `delete (msg)` on
C++26 and to a plain `delete` before it, so one written declaration serves
both dialects without a second code path.

## Decision

**The AST says whether a construct is there; the lexer says only how far it
reaches.**

`member_declaration_end` in the front end is the shape. Whether a declaration
carries a tail is `isDeletedAsWritten()` / `isDefaulted()` on the
`FunctionDecl` behind it — Sema resolved the macro, so the answer is
indifferent to spelling and to which preprocessor branch is live. Only the
tail's *extent* is lexed, and it is lexed for a delimiter rather than for a
word: forward to the first `;` at bracket depth zero, which a macro
invocation's argument list and `delete("reason")`'s message are both simply
skipped over on the way to. The recorded end is taken through
`SourceManager::getFileLoc` first, since a tail spelled by a macro can leave
it inside the expansion, where the raw lexer cannot walk; the file location
is the invocation, which is in the text the scan reads.

This is a rule about *questions*, not about the lexer. Lexing is how a
subtractive tool finds byte offsets and there is no substitute for it. What
it must not be asked is what a construct *is*: that is a question the AST has
already answered, from a translation unit that has seen the preprocessor,
and re-deriving it from the source text re-derives it wrongly for every
spelling nobody thought of.

## Rejected: teach the text test the missing spellings

The narrow fix for #99 is to resolve the macro before comparing — the
preprocessor's macro table is reachable, and `MY_DELETE` does expand to a
token sequence starting with `delete`.

It answers this report and no other. A macro that expands to another macro,
a tail assembled by concatenation, a spelling a future paper adds: each is a
new case for a test that is trying to re-derive what `isDeletedAsWritten()`
already returns. The cost of the AST gate over the text test is one pointer
at the call site, which both call sites already had in hand.

## Consequences

- Both places the tail's extent is measured go through one function:
  `\merge` / `\omit`'s whole-declaration removal and the `\freestanding`
  comment suffix's insertion point. Each used to re-find the terminating `;`
  itself, and neither does now — the semicolon search *is* the extent
  measurement, so there is one mechanism rather than two agreeing ones.
- A declaration with no tail keeps exactly the answer it had: the `;` right
  past the recorded end, or the end of that token where no `;` follows (an
  in-class body's `}`). A pure-virtual `= 0`, which Clang does record
  correctly, is not a tail by this rule and is not scanned for one.
- `tests/corpus/spec_deleted_tail_spelling.hpp` is the fixture, and it is
  written in the conditional shape the motivating library uses: which
  preprocessor branch is live must not change the rendered synopsis, since
  the declaration goes away either way.
