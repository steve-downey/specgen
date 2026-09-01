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
