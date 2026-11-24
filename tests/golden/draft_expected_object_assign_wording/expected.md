::: wording

## Assignment [expected.object.assign](){.sref} {-}

[#]{.pnum} This subclause makes use of the following exposition-only function template:

```cpp
template<class T, class U, class... Args>
constexpr void $reinit-expected$(T& newval, U& oldval, Args&&... args) {  // exposition only
  if constexpr (is_nothrow_constructible_v<T, Args...>) {
    destroy_at(addressof(oldval));
    construct_at(addressof(newval), std::forward<Args>(args)...);
  } else if constexpr (is_nothrow_move_constructible_v<T>) {
    T tmp(std::forward<Args>(args)...);
    destroy_at(addressof(oldval));
    construct_at(addressof(newval), std::move(tmp));
  } else {
    U tmp(std::move(oldval));
    destroy_at(addressof(oldval));
    try {
      construct_at(addressof(newval), std::forward<Args>(args)...);
    } catch (...) {
      construct_at(addressof(oldval), std::move(tmp));
      throw;
    }
  }
}
```

```cpp
constexpr expected& operator=(const expected& rhs);
```

[#]{.pnum} *Effects*:

- [#.#]{.pnum} If `this->has_value() && rhs.has_value()` is `true`, equivalent to `$val$ = *rhs`.
- [#.#]{.pnum} Otherwise, if `this->has_value()` is `true`, equivalent to:

  ```cpp
  $reinit-expected$($unex$, $val$, rhs.error())
  ```

- [#.#]{.pnum} Otherwise, if `rhs.has_value()` is `true`, equivalent to:

  ```cpp
  $reinit-expected$($val$, $unex$, *rhs)
  ```

- [#.#]{.pnum} Otherwise, equivalent to `$unex$ = rhs.error()`.

Then, if no exception was thrown, equivalent to: `$has_val$ = rhs.has_value(); return *this;`

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: This operator is defined as deleted unless:

- [#.#]{.pnum} `is_copy_assignable_v<T>` is `true` and
- [#.#]{.pnum} `is_copy_constructible_v<T>` is `true` and
- [#.#]{.pnum} `is_copy_assignable_v<E>` is `true` and
- [#.#]{.pnum} `is_copy_constructible_v<E>` is `true` and
- [#.#]{.pnum} `is_nothrow_move_constructible_v<T> || is_nothrow_move_constructible_v<E>` is `true`.

[#]{.pnum} This operator is trivial if:

- [#.#]{.pnum} `is_trivially_copy_constructible_v<T>` is `true`, and
- [#.#]{.pnum} `is_trivially_copy_assignable_v<T>` is `true`, and
- [#.#]{.pnum} `is_trivially_destructible_v<T>` is `true`, and
- [#.#]{.pnum} `is_trivially_copy_constructible_v<E>` is `true`, and
- [#.#]{.pnum} `is_trivially_copy_assignable_v<E>` is `true`, and
- [#.#]{.pnum} `is_trivially_destructible_v<E>` is `true`.

```cpp
constexpr expected& operator=(expected&& rhs) noexcept($see below$);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_move_constructible_v<T>` is `true` and
- [#.#]{.pnum} `is_move_assignable_v<T>` is `true` and
- [#.#]{.pnum} `is_move_constructible_v<E>` is `true` and
- [#.#]{.pnum} `is_move_assignable_v<E>` is `true` and
- [#.#]{.pnum} `is_nothrow_move_constructible_v<T> || is_nothrow_move_constructible_v<E>` is `true`.

[#]{.pnum} *Effects*:

- [#.#]{.pnum} If `this->has_value() && rhs.has_value()` is `true`, equivalent to `$val$ = std::move(*rhs)`.
- [#.#]{.pnum} Otherwise, if `this->has_value()` is `true`, equivalent to:

  ```cpp
  $reinit-expected$($unex$, $val$, std::move(rhs.error()))
  ```

- [#.#]{.pnum} Otherwise, if `rhs.has_value()` is `true`, equivalent to:

  ```cpp
  $reinit-expected$($val$, $unex$, std::move(*rhs))
  ```

- [#.#]{.pnum} Otherwise, equivalent to `$unex$ = std::move(rhs.error())`.

Then, if no exception was thrown, equivalent to: `has_val = rhs.has_value(); return *this;`

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: The exception specification is equivalent to:

```cpp
is_nothrow_move_assignable_v<T> && is_nothrow_move_constructible_v<T> &&
is_nothrow_move_assignable_v<E> && is_nothrow_move_constructible_v<E>
```

[#]{.pnum} This operator is trivial if:

- [#.#]{.pnum} `is_trivially_move_constructible_v<T>` is `true`, and
- [#.#]{.pnum} `is_trivially_move_assignable_v<T>` is `true`, and
- [#.#]{.pnum} `is_trivially_destructible_v<T>` is `true`, and
- [#.#]{.pnum} `is_trivially_move_constructible_v<E>` is `true`, and
- [#.#]{.pnum} `is_trivially_move_assignable_v<E>` is `true`, and
- [#.#]{.pnum} `is_trivially_destructible_v<E>` is `true`.

```cpp
template<class U = remove_cv_t<T>>
  constexpr expected& operator=(U&& v);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_same_v<expected, remove_cvref_t<U>>` is `false`; and
- [#.#]{.pnum} `remove_cvref_t<U>` is not a specialization of `unexpected`; and
- [#.#]{.pnum} `is_constructible_v<T, U>` is `true`; and
- [#.#]{.pnum} `is_assignable_v<T&, U>` is `true`; and
- [#.#]{.pnum} `is_nothrow_constructible_v<T, U> || is_nothrow_move_constructible_v<T> ||is_nothrow_move_constructible_v<E>` is `true`.

[#]{.pnum} *Effects*:

- [#.#]{.pnum} If `has_value()` is `true`, equivalent to: `$val$ = std::forward<U>(v);`
- [#.#]{.pnum} Otherwise, equivalent to:

  ```cpp
  $reinit-expected$($val$, $unex$, std::forward<U>(v));
  $has_val$ = true;
  ```


[#]{.pnum} *Returns*: `*this`.

```cpp
template<class G>
  constexpr expected& operator=(const unexpected<G>& e);
template<class G>
  constexpr expected& operator=(unexpected<G>&& e);
```

[#]{.pnum} Let `GF` be `const G&` for the first overload and `G` for the second overload.

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<E, GF>` is `true`; and
- [#.#]{.pnum} `is_assignable_v<E&, GF>` is `true`; and
- [#.#]{.pnum} `is_nothrow_constructible_v<E, GF> || is_nothrow_move_constructible_v<T> ||is_nothrow_move_constructible_v<E>` is `true`.

[#]{.pnum} *Effects*:

- [#.#]{.pnum} If `has_value()` is `true`, equivalent to:

  ```cpp
  $reinit-expected$($unex$, $val$, std::forward<GF>(e.error()));
  $has_val$ = false;
  ```

- [#.#]{.pnum} Otherwise, equivalent to: `$unex$ = std::forward<GF>(e.error());`

[#]{.pnum} *Returns*: `*this`.

```cpp
template<class... Args>
  constexpr T& emplace(Args&&... args) noexcept;
```

[#]{.pnum} *Constraints*: `is_nothrow_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
if (has_value()) {
  destroy_at(addressof($val$));
} else {
  destroy_at(addressof($unex$));
  $has_val$ = true;
}
return *construct_at(addressof($val$), std::forward<Args>(args)...);
```

```cpp
template<class U, class... Args>
  constexpr T& emplace(initializer_list<U> il, Args&&... args) noexcept;
```

[#]{.pnum} *Constraints*: `is_nothrow_constructible_v<T, initializer_list<U>&, Args...>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
if (has_value()) {
  destroy_at(addressof($val$));
} else {
  destroy_at(addressof($unex$));
  $has_val$ = true;
}
return *construct_at(addressof($val$), il, std::forward<Args>(args)...);
```

:::
