```cpp
namespace std {
  class tally {
  public:
    // @[tally.modifiers]{- .sref}@, modifiers
    void reset();
    void assign(long value);

    // @[tally.observers]{- .sref}@, observers
    int size() const;
    $see below$ data();

    // @[tally.ops]{- .sref}@, operations
    friend bool operator==(const tally& x, const tally& y);

  private:
    int $count$ = 0; // exposition only
  };
}
```

::: wording

## Class tally [tally]{- .sref} {-}

### Modifiers [tally.modifiers]{- .sref} {-}

```cpp
void reset();
```

[#]{.pnum} *Effects*: Equivalent to:

```cpp
$count$ = 0;
```

```cpp
void assign(long value);
```

[#]{.pnum} *Mandates*: `sizeof(long) >= sizeof(int)` is `true`.

[#]{.pnum} *Effects*: Sets the count to `value`.

### Observers [tally.observers]{- .sref} {-}

```cpp
int size() const;
```

[#]{.pnum} *Returns*: `$count$`.

```cpp
$see below$ data();
```

[#]{.pnum} *Returns*: A pointer to the count.

### Operations [tally.ops]{- .sref} {-}

```cpp
friend bool operator==(const tally& x, const tally& y);
```

[#]{.pnum} *Returns*: `x.size() == y.size()`.

:::
