::: wording

## Header synopsis [demo.syn]{- .sref} {-}

```cpp
template<typename I>
class holder {
  I $ptr$; // exposition only

public:
  constexpr explicit holder(I ptr);

  constexpr I get() const;
};

template<typename I> holder(I) -> holder<I>;
```

### Class template `holder` [demo.holder]{- .sref} {-}

```cpp
constexpr explicit holder(I ptr);
```

[#]{.pnum} *Effects*: Initializes the holder with `ptr`.

```cpp
constexpr I get() const;
```

[#]{.pnum} *Returns*: `$ptr$`.

:::
