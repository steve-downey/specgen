::: wording

## Header synopsis [demo.syn]{- .sref} {-}

```cpp
template<typename I>
class holder {
  I $ptr$; // exposition only

public:
  // @[demo.holder]{- .sref}@, construction
  constexpr explicit holder(I ptr);
};

// @[demo.ops]{- .sref}@, operations

template<typename I> void swap(holder<I>&, holder<I>&);
```

### Class template `holder` [demo.holder]{- .sref} {-}

```cpp
constexpr explicit holder(I ptr);
```

[#]{.pnum} *Effects*: Initializes the holder with `ptr`.

### Operations [demo.ops]{- .sref} {-}

```cpp
template<typename I> void swap(holder<I>&, holder<I>&);
```

[#]{.pnum} *Effects*: Exchanges the states of `a` and `b`.

:::
