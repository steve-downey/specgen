::: wording

## Assignment [resume.assign]{- .sref} {-}

```cpp
inline void assign();
```

[#]{.pnum} *Postconditions*: The stored value is one of:

- [#.#]{.pnum} the argument, if a value was supplied; this continuation belongs to the first item.
- [#.#]{.pnum} the previous value, otherwise.

Otherwise, the object is unchanged.

[#]{.pnum} A separate postcondition paragraph.

- [#.#]{.pnum} One further guarantee.

This continues the further guarantee.

[#]{.pnum} *Remarks*: A following element remains separate.

:::

::: wording

## Tables [resume.tables]{- .sref} {-}

```cpp
inline void apply();
```

[#]{.pnum} *Effects*: The following table applies.

| Condition | Effect |
|---|---|
| A | B |
: [Flat table]{#resume.flat}

Prose after the flat table.

| | First | Second |
|---|---|---|
| **Row** | C | D |
: [Cross table]{#resume.cross}

Prose after the cross table.

:::
