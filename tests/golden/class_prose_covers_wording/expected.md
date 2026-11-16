::: wording

## Class template holder [demo.holder]{- .sref} {-}

```cpp
namespace std {
  template<class E>
  class holder {};
}
```

[#]{.pnum} Subclause ([demo.holder]{- .sref}) describes the class template `holder`.

[#]{.pnum} A program that instantiates the definition of `holder` for a non-object type or an array type is ill-formed.


:::

::: wording

## Class template slot [demo.slot]{- .sref} {-}

```cpp
namespace std {
  template<class E>
  class slot {};
}
```

[#]{.pnum} A program that instantiates the definition of `slot` for a non-object type is ill-formed, and `is_array_like<E>` is `false`.


:::

::: wording

## Class template plain [demo.plain]{- .sref} {-}

```cpp
namespace std {
  template<class E>
  class plain {};
}
```

[#]{.pnum} A program that instantiates `plain<E>` is ill-formed unless `is_object_like<E>` is `true`.

[#]{.pnum} Subclause ([demo.plain]{- .sref}) describes the class template `plain`.

:::
