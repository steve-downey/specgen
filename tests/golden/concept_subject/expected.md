```cpp
namespace std {
  template<class T> using $inner-t$ = T; // exposition only
}
```

::: wording

```cpp
template<class T> void g(T&&);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `typename remove_cvref_t<T>::value_type` models `small`,
- [#.#]{.pnum} `$inner-t$<T>` models `small`,
- [#.#]{.pnum} `const T&` models `small`,
- [#.#]{.pnum} `is_integral_v<remove_cvref_t<T>>` is `true`.

[#]{.pnum} *Effects*: Does nothing.

:::

::: wording

```cpp
template<class T> void h(T&&);
```

[#]{.pnum} *Mandates*: `remove_cvref_t<T>` models `small`.

[#]{.pnum} *Effects*: Does nothing.

:::
