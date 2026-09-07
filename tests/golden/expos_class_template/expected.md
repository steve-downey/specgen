```cpp
template<typename T>
struct $box$ {
  using type = T;
}; // exposition only
```

```cpp
template<typename T>
struct $box$<T*> {
  using type = T;
}; // exposition only
```

```cpp
template<typename T>
struct $raw-box$ {
  T value;
}; // exposition only
```

```cpp
template<typename T> inline constexpr bool $boxed$ = false; // exposition only
```

```cpp
template<typename T> inline constexpr bool $boxed$<$box$<T>> = true; // exposition only
```

```cpp
template<typename LEFT, typename RIGHT, typename FALLBACK>
struct $same-model-impl$ {
  static constexpr bool value = true;
}; // exposition only
```

```cpp
template<typename MODEL_GRADE_PARAMETER, typename OPERAND_PARAMETER,
         typename FALLBACK_PARAMETER>
concept $mixes-with-model$ =
    $same-model-impl$<MODEL_GRADE_PARAMETER, OPERAND_PARAMETER,
                    FALLBACK_PARAMETER>::value; // exposition only
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
