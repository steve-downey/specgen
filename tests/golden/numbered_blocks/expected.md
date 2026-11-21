::: wording

## Blocks that open a paragraph [numbered.blocks](){.sref} {-}

```cpp
void first();
```

[#]{.pnum}

::: note

A note that is the first thing in a description opens its paragraph, as it always has.

:::

[#]{.pnum} *Returns*: A reference to the element.

::: note

A note in the paragraph before it, which is where an authored note goes.

:::

[#]{.pnum}

::: note

A note the draft numbered by itself.

:::

Prose after that note is the rest of its paragraph.

[#]{.pnum}

::: example

```cpp
first();
```

:::

[#]{.pnum}

```cpp
second();
```

[#]{.pnum}

- [#.#]{.pnum} One.
- [#.#]{.pnum} Two.

[#]{.pnum} *Remarks*: The element after them is numbered as before.

:::
