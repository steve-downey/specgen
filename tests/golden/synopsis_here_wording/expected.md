::: wording

## Class template holder [demo.holder]{- .sref} {-}

[#]{.pnum} Subclause ([demo.holder]{- .sref}) describes the class template `holder`.

```cpp
namespace std {
  template<class E>
  class holder {};
}
```

[#]{.pnum} A program that instantiates the definition of `holder` for a non-object type is ill-formed.

:::

::: wording

## Class template plain [demo.plain]{- .sref} {-}

[#]{.pnum} Subclause ([demo.plain]{- .sref}) describes the class template `plain`.

[#]{.pnum} It holds nothing.

```cpp
namespace std {
  template<class E>
  class plain {};
}
```

[#]{.pnum} A program that instantiates `plain<E>` is ill-formed unless `is_object_like<E>` is `true`.

[#]{.pnum} *Remarks*: A `plain` is trivially copyable.

:::

::: wording

## Class template late [demo.late]{- .sref} {-}

```cpp
namespace std {
  template<class E>
  class late {};
}
```

[#]{.pnum} Subclause ([demo.late]{- .sref}) describes the class template `late`.

:::

::: wording

## Class template routed [demo.routed]{- .sref} {-}

```cpp
namespace std {
  template<class E>
  class routed {};
}
```

### General [demo.routed.general]{- .sref} {-}

[#]{.pnum} Subclause ([demo.routed]{- .sref}) describes the class template `routed`.

[#]{.pnum} *Remarks*: A `routed` is described in its general subclause.

:::
