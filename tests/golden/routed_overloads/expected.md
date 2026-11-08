::: wording

## Header <routed> synopsis [routed.syn]{- .sref} {-}

```cpp
namespace std {
  class gadget {
  public:
    // @[gadget.ctor]{- .sref}@, constructors
    constexpr gadget() noexcept;
    int size() const;
    constexpr gadget(int) noexcept;
    constexpr gadget(long) noexcept;
    constexpr gadget(short) noexcept;

    // @[gadget.obs]{- .sref}@, observers
    using value_type = int;
    using reference = int&;

    static constexpr int first = 1;
    static constexpr int second = 2;

    friend void swap(gadget&, gadget&);
    friend void swap(gadget&, int);
  };

  // @[gadget.obs]{- .sref}@, namespace observers

  int observe(gadget&);

  int observe(gadget&, int);

  int observe(gadget&, long);

  template<class T> class specialized;

  template<>
  class specialized<int> {
  public:
    // @[gadget.ctor]{- .sref}@, constructors
    specialized();
    specialized(int);
  };

  template<>
  class specialized<long> {
  public:
    // @[gadget.ctor]{- .sref}@, constructors
    specialized();
    specialized(long);
  };
}
```

### Constructors [gadget.ctor]{- .sref} {-}

```cpp
constexpr gadget() noexcept;
constexpr gadget(int) noexcept;
constexpr gadget(long) noexcept;
constexpr gadget(short) noexcept;
```

[#]{.pnum} *Postconditions*: Has no value.

```cpp
int size() const;
```

[#]{.pnum} *Returns*: Zero.

```cpp
specialized();
specialized(int);
```

[#]{.pnum} *Postconditions*: Has an integer value.

```cpp
specialized();
specialized(long);
```

[#]{.pnum} *Postconditions*: Has a long value.

### Observers [gadget.obs]{- .sref} {-}

```cpp
using value_type = int;
using reference = int&;
```

[#]{.pnum} *Remarks*: The value type.

```cpp
static constexpr int first = 1;
static constexpr int second = 2;
```

[#]{.pnum} *Remarks*: The first constant.

```cpp
friend void swap(gadget&, gadget&);
friend void swap(gadget&, int);
```

[#]{.pnum} *Effects*: Exchanges values.

```cpp
int observe(gadget&);
int observe(gadget&, int);
int observe(gadget&, long);
```

[#]{.pnum} *Returns*: Zero.

:::
