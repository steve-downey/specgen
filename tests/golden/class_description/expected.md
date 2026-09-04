```cpp
struct tag {};
```

::: wording

[#]{.pnum} *Remarks*: A defined record's own description.

:::

```cpp
template<class T>
struct checked {};
```

::: wording

[#]{.pnum} *Mandates*: `T` is a type this facility accepts.

[#]{.pnum} *Remarks*: Instantiating `checked` registers nothing; it only constrains.

:::

```cpp
template<class T>
struct probe {};
```

::: wording

[#]{.pnum} A program that instantiates `probe<T>` is ill-formed unless `acceptable_v<T>` is `true`.

:::

::: wording

[#]{.pnum} *Remarks*: The derived paragraph above states the requirement; this says what the type is for.

:::

```cpp
template<class T>
struct box {
  // @[box.mem]{- .sref}@, member types
  using type = T;
};
```

::: wording

[#]{.pnum} *Remarks*: A `box` is a distinct type for each `T`; two `box` types with different `T` never compare equal.

:::

::: wording

## Member types [box.mem]{- .sref} {-}

```cpp
using type = T;
```

[#]{.pnum} *Remarks*: The contained type.

:::
