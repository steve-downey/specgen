::: wording

## Class `box` [demo.box]{- .sref} {-}

```cpp
struct box {
  // @[demo.box]{- .sref}@, observers

  template<class T>
    requires requires(T t) {
      { t == 0 };
    }
  constexpr int with_req_expr(T) const;

  template<class T>
    requires nonzero<T>
  constexpr int with_named(T) const;

  template<class I>
    requires requires(I i) {
      { *i == 0 };
    }
  friend constexpr bool operator==(const I& it, box);
};
```

```cpp
template<class T>
  requires requires(T t) {
    { t == 0 };
  }
constexpr int with_req_expr(T) const;
```

[#]{.pnum} *Returns*: `0`.

```cpp
template<class T>
  requires nonzero<T>
constexpr int with_named(T) const;
```

[#]{.pnum} *Returns*: `1`.

```cpp
template<class I>
  requires requires(I i) {
    { *i == 0 };
  }
friend constexpr bool operator==(const I& it, box);
```

[#]{.pnum} *Returns*: Whether `it` is at the end.

:::

::: wording

## Free functions [demo.free]{- .sref} {-}

```cpp
template<class T>
  requires requires(T t) {
    { t == 0 };
  }
constexpr int free_with_req_expr(T);
```

[#]{.pnum} *Returns*: `2`.

```cpp
template<class T>
  requires nonzero<T>
constexpr int free_with_named(T);
```

[#]{.pnum} *Returns*: `3`.

:::
