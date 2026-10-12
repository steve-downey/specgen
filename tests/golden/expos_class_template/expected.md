```cpp
namespace std {
  template<typename T>
  struct $box$ {
    using type = T;
  }; // exposition only
}
```

```cpp
namespace std {
  template<typename T>
  struct $box$<T*> {
    using type = T;
  }; // exposition only
}
```

```cpp
namespace std {
  template<typename T>
  struct $raw-box$ {
    T value;
  }; // exposition only
}
```

```cpp
namespace std {
  template<typename T> inline constexpr bool $boxed$ = false; // exposition only
}
```

```cpp
namespace std {
  template<typename T> inline constexpr bool $boxed$<$box$<T>> = true; // exposition only
}
```

```cpp
namespace std {
  template<typename LEFT, typename RIGHT, typename FALLBACK>
  struct $same-model-impl$ {
    static constexpr bool value = true;
  }; // exposition only
}
```

```cpp
namespace std {
  template<typename MODEL_GRADE_PARAMETER, typename OPERAND_PARAMETER,
           typename FALLBACK_PARAMETER>
  concept $mixes-with-model$ =
      $same-model-impl$<MODEL_GRADE_PARAMETER, OPERAND_PARAMETER,
                      FALLBACK_PARAMETER>::value; // exposition only
}
```

```cpp
namespace std {
  template<typename T>
  struct $holder$ {
    using value_type = T;

    constexpr T get() const;

    constexpr bool empty() const;
  }; // exposition only
}
```

::: wording

```cpp
constexpr T get() const;
```

[#]{.pnum} *Returns*: The held value.

:::

::: wording

```cpp
constexpr bool empty() const;
```

[#]{.pnum} *Returns*: `false`.

:::

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
