::: wording

## Constructors [optional.ctor]{- .sref} {-}

```cpp
constexpr optional() noexcept;
constexpr optional(nullopt_t) noexcept;
```

[#]{.pnum} *Postconditions*: `*this` does not contain a value.

```cpp
constexpr optional(const optional& rhs);
```

[#]{.pnum} *Constraints*: `is_copy_constructible_v<T>` is `true` and `is_trivially_copy_constructible_v<T>` is `false`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

```cpp
template<class... Args> constexpr explicit optional(in_place_t, Args&&... args);
```

[#]{.pnum} *Constraints*: `is_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Direct-non-list-initializes the contained value with `std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `has_value()` is `true`.

```cpp
template<class U>
constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, const U&>` is `true`,
- [#.#]{.pnum} `is_convertible_v<U, T>` is `true`,
- [#.#]{.pnum} `is_same_v<T, U>` is `false`,
- [#.#]{.pnum} `is_constructible_v<T, optional<U>>` is `false`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Postconditions*: `has_value()` is equal to `rhs.has_value()`.

:::
