```cpp
template<typename T>
struct $box$ {
  using type = T;
}; // exposition only
```

```cpp
template<typename T>
struct $raw-box$ {
  T value;
}; // exposition only
```

::: wording

```cpp
template<typename T> constexpr $box$<T> make(T t);
```

[#]{.pnum} *Returns*: A boxed copy of `t`.

:::

::: wording

```cpp
template<typename T> constexpr $raw-box$<T> unwrap($box$<T> b);
```

[#]{.pnum} *Returns*: The raw form of `b`.

:::
