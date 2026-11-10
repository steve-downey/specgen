# Issue #253: preserve line-leading LaTeX macros

Line wrapping must not decide whether a draft macro survives a docblock.
This PR follows #254 and releases as 0.21.0 after its 0.20.0.

1. Classify tag heads against the element and marker registries and the block
   keywords before dispatching table or body syntax. Keep the deprecated
   `\returns-equiv` spelling reserved for its migration diagnostic.
2. Let other commands with braced or optional arguments, starred commands,
   and control symbols flow through the ordinary prose path. Unknown bare
   commands still report an Error for typo detection, with their full line
   retained as prose and the diagnostic explaining that recovery.
3. Test wrapping equivalence, leading prose, lists, both table kinds, notes,
   examples, and preserved unknown text. Keep code and verbatim payloads exact.
4. Add a corpus case for the reported Preconditions wording and macro-led
   continuations. Generate, round-trip, validate, render, and split it through
   all three backends; pin the changed diagnostic spelling in existing cases.
5. Document the grammar, regenerate goldens and captures, run the full suite
   and lint, and bump all four version files to 0.21.0.

Open a draft PR based on #254 with validation results and reviewed output.
