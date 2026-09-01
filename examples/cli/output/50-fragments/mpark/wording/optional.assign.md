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
