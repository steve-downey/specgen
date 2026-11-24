::: wording

## Swap [expected.object.swap](){.sref} {-}

```cpp
constexpr void swap(expected& rhs) noexcept($see below$);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_swappable_v<T>` is `true` and
- [#.#]{.pnum} `is_swappable_v<E>` is `true` and
- [#.#]{.pnum} `is_move_constructible_v<T> && is_move_constructible_v<E>` is `true`, and
- [#.#]{.pnum} `is_nothrow_move_constructible_v<T> || is_nothrow_move_constructible_v<E>` is `true`.

[#]{.pnum} *Effects*: See [Table \[expected.object.swap\]](#expected.object.swap).

| | `this->has_value()` | `!this->has_value()` |
|---|---|---|
| **`rhs.has_value()`** | equivalent to: `using std::swap; swap($val$, rhs.$val$);` | calls `rhs.swap(*this)` |
| **`!rhs.has_value()`** | `$see below$` | equivalent to: `using std::swap; swap($unex$, rhs.$unex$);` |
: [`swap(expected&)` effects]{#expected.object.swap}

For the case where `rhs.has_value()` is `false` and `this->has_value()` is `true`, equivalent to:

```cpp
if constexpr (is_nothrow_move_constructible_v<E>) {
  E tmp(std::move(rhs.$unex$));
  destroy_at(addressof(rhs.$unex$));
  try {
    construct_at(addressof(rhs.$val$), std::move($val$));
    destroy_at(addressof($val$));
    construct_at(addressof($unex$), std::move(tmp));
  } catch(...) {
    construct_at(addressof(rhs.$unex$), std::move(tmp));
    throw;
  }
} else {
  remove_cv_t<T> tmp(std::move($val$));
  destroy_at(addressof($val$));
  try {
    construct_at(addressof($unex$), std::move(rhs.$unex$));
    destroy_at(addressof(rhs.$unex$));
    construct_at(addressof(rhs.$val$), std::move(tmp));
  } catch (...) {
    construct_at(addressof($val$), std::move(tmp));
    throw;
  }
}
$has_val$ = false;
rhs.$has_val$ = true;
```

[#]{.pnum} *Throws*: Any exception thrown by the expressions in the *Effects*.

[#]{.pnum} *Remarks*: The exception specification is equivalent to:

```cpp
is_nothrow_move_constructible_v<T> && is_nothrow_swappable_v<T> &&
is_nothrow_move_constructible_v<E> && is_nothrow_swappable_v<E>
```

```cpp
friend constexpr void swap(expected& x, expected& y) noexcept(noexcept(x.swap(y)));
```

[#]{.pnum} *Effects*: Equivalent to `x.swap(y)`.

:::
