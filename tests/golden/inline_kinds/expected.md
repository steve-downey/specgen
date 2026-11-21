::: wording

## Placeholders and named requirements [gadget.placeholders](){.sref} {-}

[#]{.pnum} The header provides specializations for each combination of the placeholders *cv*, *ref*, and *noex*, where *cv* is `const` or empty.

[#]{.pnum} The type `T` meets the *Cpp17Destructible* requirements, and *inv-quals* is *cv*`&`.

[#]{.pnum} Placeholders sit against punctuation: (*ref*), *noex*-qualified, *Cpp17MoveConstructible*.

[#]{.pnum} A revision [requires *Cpp17CopyConstructible*]{.rm}[requires *Cpp17MoveConstructible* when *noex* holds]{.add}.

[#]{.pnum} See [Table \[gadget.table\]](#gadget.table) lists them, and `\term{unknown}`{=latex} stays as written.

[#]{.pnum} Within subclause [gadget.placeholders](){.sref}, a type that satisfies `$gadget-like$` models `copyable` for some *cv2* ([gadget.table](){.sref}).

[#]{.pnum} A revision [cites ([gadget.table](){.sref})]{.rm}[cites subclause [gadget.table](){.sref} for `$gadget-like$` and *cv2*]{.add}.

[#]{.pnum} A long implementation meets *Cpp17UnaryTypeTrait*, *long-name* and `invocable`.

:::
