::: wording

## Class `result` [demo.result]{- .sref} {-}

```cpp
struct result {
  char32_t code_point{};
  bool is_error{false};

  int $scratch${0}; // exposition only
};
```

[#]{.pnum} *Remarks*: `code_point` is the decoded value, and `is_error` says whether the decode failed; when it did, `code_point` is U+FFFD.

:::
