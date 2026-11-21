::: wording

## Class template box [box.class]{- .sref} {-}

```cpp
namespace std {
  template<class T>
  class box {
  public:
    // @[box.ops]{- .sref}@, operations
    template<class F> constexpr void show(F&& f) const;

  private:
    T* $val$ = nullptr; // exposition only
  };
}
```

[#]{.pnum} An authored code block names an exposition-only member with the draft's escape, which every backend writes in its own form (issue #406).

:::

::: wording

## Operations [box.ops]{- .sref} {-}

```cpp
template<class F>
  constexpr void show(F&& f) const;
```

[#]{.pnum} *Mandates*: The declaration

```cpp
auto u(invoke(std::forward<F>(f), *$val$));
```

is well-formed.

[#]{.pnum} *Effects*: Calls `invoke(std::forward<F>(f), *$val$)`.

:::
