::: wording

## Class template `demo_view` [demo.view]{- .sref} {-}

```cpp
template<class R>
class demo_view {
  R $base$; // exposition only

  class $iterator$; // exposition only

  struct $state$ {
    bool started = false;
  }; // exposition only

public:
  // @[demo.view]{- .sref}@, access
  constexpr $iterator$ begin() const;
};
```

```cpp
constexpr $iterator$ begin() const;
```

[#]{.pnum} *Returns*: An iterator over the base range, positioned at its first element.

### Class `demo_view::iterator` [demo.view.iterator]{- .sref} {-}

```cpp
constexpr int operator*() const;
```

[#]{.pnum} *Returns*: Equivalent to:

```cpp
return $value$;
```

:::
