```cpp
namespace std {
  template<class T>
  class gauge {
  public:
    // @[gauge.members]{- .sref}@, static members
    static constexpr bool consumes = true;

    static constexpr int digits = 8 * sizeof(T);

    template<class U> static constexpr bool accepts = sizeof(U) >= sizeof(T);

    static constexpr int version = 1;

    static constexpr int limit = 100;

    // @[gauge.ops]{- .sref}@, operations
    constexpr T read() const;
  };
}
```

::: wording

## Class template gauge [gauge]{- .sref} {-}

### Static members [gauge.members]{- .sref} {-}

```cpp
static constexpr bool consumes = true;
```

[#]{.pnum} *Remarks*: `true` when a gauge consumes its argument.

```cpp
static constexpr int digits;
```

[#]{.pnum} *Remarks*: The number of significant bits in a reading.

```cpp
template<class U> static constexpr bool accepts = sizeof(U) >= sizeof(T);
```

[#]{.pnum} *Remarks*: `true` when a reading converts to `U`.

### Limits [gauge.limits]{- .sref} {-}

```cpp
static constexpr int limit = 100;
```

[#]{.pnum} *Remarks*: The largest reading.

### Operations [gauge.ops]{- .sref} {-}

```cpp
constexpr T read() const;
```

[#]{.pnum} *Returns*: A default reading.

:::
