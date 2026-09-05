```cpp
struct adaptor {};
```

```cpp
template<typename T>
struct tagger {};
```

::: wording

```cpp
inline constexpr $unspecified$ thing;
```

[#]{.pnum} *Remarks*: The name `thing` denotes a customization point object.

:::

::: wording

```cpp
inline constexpr $unspecified$ copied;
```

[#]{.pnum} *Remarks*: The copy-initialized spelling masks the same way.

:::

::: wording

```cpp
template<typename T> inline constexpr $unspecified$ tag;
```

[#]{.pnum} *Remarks*: The name `tag` denotes a family of tag objects.

:::
