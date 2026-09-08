::: wording

## Errors [demo.errors]{- .sref} {-}

```cpp
enum class whatwg_error { invalid_byte, truncated_sequence };
```

[#]{.pnum} *Remarks*: The enumerators have the meanings in the following table.

| Constant | Meaning |
|---|---|
| `invalid_byte` | the input holds a byte the encoding does not allow in that position. |
| `truncated_sequence` | the input ends in the middle of a sequence. |
: [Enum class `whatwg_error`]{#demo.errors.tab}

:::
