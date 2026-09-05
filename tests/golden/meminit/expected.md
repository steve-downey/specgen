::: wording

## Class template `holder` [demo.holder]{- .sref} {-}

```cpp
template<typename R>
class holder {
  R $base$;   // exposition only
  int $count$; // exposition only

public:
  // @[demo.holder]{- .sref}@, construction
  constexpr explicit holder(R base);

  constexpr holder();
};
```

```cpp
constexpr explicit holder(R base);
```

[#]{.pnum} *Effects*: Initializes `$base$` with `std::move(base)` and `$count$` with `1`.

```cpp
constexpr holder();
```

[#]{.pnum} *Effects*: Equivalent to `holder(R())`.

:::
