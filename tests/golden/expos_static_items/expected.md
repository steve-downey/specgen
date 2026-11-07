```cpp
namespace std {
  class predicate {
  public:
    // @[predicate.members]{- .sref}@, exposition members
    static constexpr bool $test-value$ = true; // exposition only

  private:
    template<class T> static constexpr bool $accepts-type$ = $see below$; // exposition only

    static constexpr int $storage-limit${42}; // exposition only

    static constexpr int $marker-only$ = 8; // exposition only
  };
}
```

::: wording

## Class predicate [predicate]{- .sref} {-}

### Exposition members [predicate.members]{- .sref} {-}

```cpp
static constexpr bool $test-value$ = true;
```

[#]{.pnum} *Remarks*: `$test-value$` is always true.

```cpp
template<class T> static constexpr bool $accepts-type$ = $see below$;
```

[#]{.pnum} `$accepts-type$<T>` tests whether the type can be called.

::: example

For an ordinary type:

```cpp
static_assert(true);
```

:::

### Limits [predicate.limits]{- .sref} {-}

```cpp
static constexpr int $storage-limit${42};
```

[#]{.pnum} *Remarks*: The limit used by `$storage-limit$`.

:::
