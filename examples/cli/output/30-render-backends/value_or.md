::: wording

```cpp
template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& v) const &;
```

[#]{.pnum} *Mandates*: `is_copy_constructible_v<T>` is `true` and `is_convertible_v<U, T>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return has_value() ? **this : static_cast<remove_cv_t<T>>(std::forward<U>(v));
```

:::
