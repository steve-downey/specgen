```cpp
namespace std {
  class table {
  public:
    // @[table.ops]{- .sref}@, operations
    constexpr void reset();

  private:
    int $limit$ = 0; // exposition only

    static constexpr int $capacity$ = 8; // exposition only

    template<class T> static constexpr T $spare$ = T(2); // exposition only
  };
}
```

::: wording

## Operations [table.ops]{- .sref} {-}

```cpp
constexpr void reset();
```

[#]{.pnum} *Effects*: Equivalent to:

```cpp
$limit$ = $capacity$ + $spare$<int>;
```

:::
