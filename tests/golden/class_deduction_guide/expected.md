::: wording

## Class template `holder` [demo.holder]{- .sref} {-}

```cpp
namespace std {
  template<class I>
  class holder {
  public:
    // @[demo.holder.cons]{- .sref}@, constructors
    constexpr explicit holder(I ptr);
    constexpr holder(I ptr, int count);

  private:
    I $ptr$; // exposition only
  };

  template<class I> holder(I) -> holder<I>;

  template<class I> holder(I, int) -> holder<I>;
}
```

### Constructors [demo.holder.cons]{- .sref} {-}

```cpp
constexpr explicit holder(I ptr);
```

[#]{.pnum} *Effects*: Initializes `ptr` with `ptr`.

```cpp
constexpr holder(I ptr, int count);
```

[#]{.pnum} *Effects*: Initializes `ptr` with `ptr`.

:::
