```cpp
namespace std {
  class span_like {
  public:
    // @[span.like.obs]{- .sref}@, observers
    @[constexpr]{.add}@ int size() const;
    @[constexpr]{.add}@ bool empty() const;
    @[constexpr]{.add}@ int front() const;
  };
}
```

::: wording

## Observers [span.like.obs]{- .sref} {-}

```cpp
@[constexpr]{.add}@ int size() const;
```

[…]

```cpp
@[constexpr]{.add}@ bool empty() const;
```

[…]

```cpp
@[constexpr]{.add}@ int front() const;
```

[#]{.pnum} *Preconditions*: [`empty()` is `false`.]{.rm}[the span is not empty.]{.add}

[#]{.pnum} *Returns*: The first element.

:::
