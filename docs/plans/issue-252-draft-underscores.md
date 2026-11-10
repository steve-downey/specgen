# Issue #252: literal underscores in draft LaTeX

The draft loads the `underscore` package, so `_` stays literal in text,
including `\tcode` arguments. Issue #248 added an unnecessary escape.

1. Remove `_` from `draft_escape_code_text`'s escape cases. Keep the other
   escapes and the shared span walk so Org's LaTeX snippets agree with LaTeX.
2. Update the backend contract in architecture §8 and the existing assertions
   for identifiers, table cells, and literals around semantic spans.
3. Add regression coverage for the reported Mandates wording, prose, and
   changed table content. Extend the inline-code corpus with a type trait.
4. Regenerate and inspect all goldens and example captures. Confirm changes
   remove underscore escapes only, apart from the added corpus wording.
5. Run full ctest and `make lint`, verify a LaTeX document loading `underscore`,
   and bump all four version files from 0.19.0 to 0.20.0 as requested.

Open a draft PR with the reviewed generated changes and validation results.
