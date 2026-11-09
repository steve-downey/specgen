::: wording

## General [escaping.general]{- .sref} {-}

[#]{.pnum} A reference qualifier is one of the following:

- [#.#]{.pnum} `&`, or
- [#.#]{.pnum} `&&`.

Special characters in code are literal: `# % _ { } ~ ^ $ \`.

:::

::: wording

## References [escaping.references]{- .sref} {-}

```cpp
inline int& alias(int& value);
```

[#]{.pnum} *Returns*: `value`.

[#]{.pnum} *Remarks*: A reference is written `int&`.

| Token | Meaning & spelling |
|---|---|
| `&&` | The tokens `&` & `&&` denote reference qualifiers. |
: [Reference tokens `&` and `&&`]{#escaping.tokens}

:::
