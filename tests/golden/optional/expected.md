```cpp
struct nullopt_t {};
```

```cpp
struct in_place_t {
  explicit in_place_t() = default;
};
```

```cpp
template<class T>
class optional {
public:
  using value_type = T;

  // @[optional.ctor]{- .sref}@, constructors
  constexpr optional() noexcept;
  constexpr optional(nullopt_t) noexcept;

  constexpr optional(const optional& rhs)
    requires is_copy_constructible_v<T> && (!is_trivially_copy_constructible_v<T>);

  template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args)
    requires is_constructible_v<T, Args...>;

  template<class U>
  constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs)
    requires is_constructible_v<T, const U&> && is_convertible_v<U, T> &&
             (!is_same_v<T, U>) && (!is_constructible_v<T, optional<U>>);

  // @[optional.assign]{- .sref}@, assignment
  template<class... Args> constexpr T& emplace(Args&&... args);

  // @[optional.observe]{- .sref}@, observers
  constexpr bool has_value() const noexcept;
  constexpr const T& operator*() const&;

  template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& u) const&;

  // @[optional.monadic]{- .sref}@, monadic operations
  template<class F> constexpr $see below$ transform(F&& f) const&;

  // @[optional.mod]{- .sref}@, modifiers
  constexpr void reset() noexcept;

  friend constexpr bool operator==(const optional& x, const optional& y);

private:
  T $value$;            // exposition only
  bool $engaged$ = false; // exposition only
};
```

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

::: wording

## Assignment [optional.assign]{- .sref} {-}

```cpp
template<class... Args> constexpr T& emplace(Args&&... args);
```

[#]{.pnum} *Mandates*: `is_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Destroys any contained value, then direct-non-list-initializes the contained value with `std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `has_value()` is `true`.

[#]{.pnum} *Returns*: A reference to the new contained value.

:::

::: wording

## Observers [optional.observe]{- .sref} {-}

```cpp
constexpr bool has_value() const noexcept;
```

[#]{.pnum} *Returns*: `true` if and only if `*this` contains a value.

```cpp
constexpr const T& operator*() const&;
```

[#]{.pnum} *Hardened preconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: A reference to the contained value.

```cpp
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& u) const&;
```

[#]{.pnum} *Mandates*: `is_copy_constructible_v<T>` is `true` and `is_convertible_v<U, T>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return has_value() ? **this : static_cast<remove_cv_t<T>>(u);
```

:::

::: wording

## Monadic operations [optional.monadic]{- .sref} {-}

```cpp
template<class F> constexpr $see below$ transform(F&& f) const&;
```

[#]{.pnum} *Effects*: If `*this` contains a value, returns an optional holding the result of invoking `f` with the contained value; otherwise returns an empty optional.

[#]{.pnum} *Remarks*: The return type is `remove_cvref_t<invoke_result_t<F, const T&>>`.

:::

::: wording

## Modifiers [optional.mod]{- .sref} {-}

```cpp
constexpr void reset() noexcept;
```

[#]{.pnum} *Effects*: Equivalent to:

```cpp
$engaged$ = false;
```

[#]{.pnum} *Postconditions*: `has_value()` is `false`.

```cpp
friend constexpr bool operator==(const optional& x, const optional& y);
```

[#]{.pnum} *Returns*: `true` if both operands are disengaged, or both are engaged with equal contained values.

:::
