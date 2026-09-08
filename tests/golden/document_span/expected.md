::: wording

## Header `<span>` synopsis [span.syn]{- .sref} {-}

```cpp
// @[span.errors]{- .sref}@, error types

enum class error_kind { bad_input, truncated };

template<class T> inline constexpr bool flag = false;

inline constexpr bool flag_int = flag<int>;

inline constexpr bool flag_char = flag<char>;

class widget {
  int $count$ = 0; // exposition only

public:
  // @[span.widget]{- .sref}@, observers
  int count() const;
};
```

:::

::: wording

## Error types [span.errors]{- .sref} {-}

```cpp
enum class error_kind { bad_input, truncated };
```

[#]{.pnum} *Remarks*: The enumerators have the following meanings:

- [#.#]{.pnum} `bad_input` -- the input is not what the encoding allows.
- [#.#]{.pnum} `truncated` -- the input ends in the middle of a sequence.

```cpp
template<class T> inline constexpr bool flag = false;
```

[#]{.pnum} *Remarks*: `flag<T>` is `true` for a type the encoding admits.

:::

::: wording

## Class `widget` [span.widget]{- .sref} {-}

```cpp
int count() const;
```

[#]{.pnum} *Returns*: The number of things.

:::
