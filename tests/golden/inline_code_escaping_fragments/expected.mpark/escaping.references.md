::: wording

## References [escaping.references]{- .sref} {-}

```cpp
inline int& alias(int& value);
```

[#]{.pnum} *Mandates*: `is_constructible_v<int&, int&>` is `true`.

[#]{.pnum} *Returns*: `value`.

[#]{.pnum} *Remarks*: A reference is written `int&`.

| Token | Meaning & spelling |
|---|---|
| `&&` | The tokens `&` & `&&` denote reference qualifiers. |
: [Reference tokens `&` and `&&`]{#escaping.tokens}

:::
