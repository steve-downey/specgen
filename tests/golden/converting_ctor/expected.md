::: wording

```cpp
template<class U> constexpr explicit($see below$) optional(const optional<U>& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, const U&>` is `true`,
- [#.#]{.pnum} `is_constructible_v<T, optional<U>&>` is `false`,
- [#.#]{.pnum} `is_constructible_v<T, optional<U>>` is `false`,
- [#.#]{.pnum} `is_convertible_v<optional<U>&, T>` is `false`.

[#]{.pnum} *Effects*: If `rhs` contains a value, direct-non-list-initializes the contained value with `*rhs`.

[#]{.pnum} *Remarks*: The expression inside `explicit` is equivalent to `!is_convertible_v<const U&, T>`, see ([optional.general]{- .sref}).

:::
