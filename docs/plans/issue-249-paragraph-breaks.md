# Issue #249: paragraph boundaries after lists and tables

One blank decorated line after a list or table preserves paragraph
continuation. Two consecutive blank decorated lines make the following prose
start a new numbered paragraph.

1. Track consecutive blanks in the docblock parser. At the second blank,
   clear list state and the pending paragraph continuation. Leave blanks in
   table contents and authored code under their existing rules.
2. Reuse `BodyParagraph::continuation`; the IR and all three backends already
   distinguish a continued paragraph from a new one.
3. Test one, two, and three blanks in leading and labelled bodies; both table
   kinds; block-comment decoration; state reset; and literal code blanks.
4. Add an end-to-end corpus case with consecutive paragraphs ending in lists,
   and paragraphs after both table kinds. Render and split every backend.
5. Document the rule, regenerate goldens and captures, run full ctest and lint,
   and use 0.19.0 for the release following #248 (0.18.0).

Inspect the generated paragraph boundaries before committing. Authored prose
and FormatStyle stay unchanged; the paragraph rule is the proposal in #249.
