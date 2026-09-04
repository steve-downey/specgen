::: wording

## Vocabulary [demo.vocab]{- .sref} {-}

```cpp
template<class T> using id_t = T;
```

[#]{.pnum} *Remarks*: The identity alias.

```cpp
using value_t = int;
using size_type = unsigned long;
```

[#]{.pnum} *Remarks*: The value aliases.

```cpp
using token_t = $implementation-defined$;
```

[#]{.pnum} *Remarks*: The token type is implementation-defined.

```cpp
template<class T> inline constexpr bool flag_v = false;
```

[#]{.pnum} *Remarks*: `flag_v<T>` is `false` unless a program specializes it.

```cpp
inline constexpr int max_links = 8;
```

[#]{.pnum} *Remarks*: The registration limit.

```cpp
template<class T>
concept usable = requires { typename T::type; };
```

[#]{.pnum} *Remarks*: A type is usable if it names a member type.

:::
