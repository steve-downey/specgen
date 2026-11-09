# Issue #248: inline code in draft LaTeX

The draft's `\tcode` macro takes a text-mode argument. Literal C++ punctuation
must be escaped before semantic span macros are inserted.

1. Give the shared span walker an optional literal-text escape function.
   Keep its identity default for listings and the other backend conventions.
2. Escape LaTeX special characters in `draft_code_inline`, shared with Org's
   LaTeX export snippets. Preserve whole-span macros and source byte offsets.
3. Escape table prose before assembling its inline pieces so code ampersands
   are escaped once, including inside change marks.
4. Cover reference qualifiers, special characters, semantic spans, table cells,
   change marks, literal listings, and Org snippets. Add a corpus case rendered
   and split through all three backends.
5. Regenerate goldens and example captures, run the full suite and lint, and
   compile the reproduced inline code with LaTeX. Bump 0.17.0 to 0.18.0.

Inspect generated escaping before committing. Authored prose and FormatStyle
stay unchanged; this fixes the LaTeX spelling of literal code characters.
