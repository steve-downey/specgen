::: wording

## Class template box [box.class](){.sref} {-}

```cpp
namespace std {
  template<class T>
  class box {
  public:
    // @[box.ops](){.sref}@, operations
    template<class F> constexpr auto apply(F&& f) const;
    template<class F> constexpr auto chain(F&& f) const;
    template<class F> constexpr auto local(F&& f) const;
    constexpr auto member() const;
    template<class F> constexpr auto prose(F&& f) const;
    template<class F> constexpr auto triple(F&& f) const;
    template<class F> constexpr auto joined(F&& f) const;

  private:
    T* $val$ = nullptr; // exposition only
  };
}
```

[#]{.pnum} The leading aliases that a static_assert names are introduced by one derived "Let" paragraph ahead of the Mandates, and leave the Effects code (issue #404, decision let-binding-wording).

:::

::: wording

## Operations [box.ops](){.sref} {-}

```cpp
template<class F>
  constexpr auto apply(F&& f) const;
```

[#]{.pnum} Let `U` be `invoke_result_t<F, T&>`.

[#]{.pnum} *Mandates*: `is_object_v<U>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return invoke(std::forward<F>(f), *$val$);
```

```cpp
template<class F>
  constexpr auto chain(F&& f) const;
```

[#]{.pnum} Let `U` be `invoke_result_t<F, T&>` and `V` be `remove_cvref_t<U>`.

[#]{.pnum} An alias the asserted one names is introduced too, in source order.

[#]{.pnum} *Mandates*: `is_object_v<V>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return invoke(std::forward<F>(f), *$val$);
```

```cpp
template<class F>
  constexpr auto local(F&& f) const;
```

[#]{.pnum} An alias only the body uses stays in the code.

[#]{.pnum} *Mandates*: `is_object_v<T>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
using W = invoke_result_t<F, T&>;

return W(invoke(std::forward<F>(f), *$val$));
```

```cpp
constexpr auto member() const;
```

[#]{.pnum} Let `U` be `decltype(($val$))`.

[#]{.pnum} *Mandates*: `is_const_v<U>` is `false`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return *$val$;
```

```cpp
template<class F>
  constexpr auto prose(F&& f) const;
```

[#]{.pnum} Let `U` be `invoke_result_t<F, T&>`.

[#]{.pnum} `U` names the result of calling `f`.

[#]{.pnum} *Mandates*: `is_object_v<U>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return invoke(std::forward<F>(f), *$val$);
```

```cpp
template<class F>
  constexpr auto triple(F&& f) const;
```

[#]{.pnum} Let `U` be `invoke_result_t<F, T&>`, `V` be `remove_cvref_t<U>`, and `W` be `remove_cvref_t<V>`.

[#]{.pnum} Three aliases are bound in one sentence, the last after ", and".

[#]{.pnum} *Mandates*: `is_object_v<W>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return invoke(std::forward<F>(f), *$val$);
```

```cpp
template<class F>
  constexpr auto joined(F&& f) const;
```

[#]{.pnum} Let `U` be `invoke_result_t<F, T&>` and `V` be `remove_cvref_t<U>`.

[#]{.pnum} *Mandates*: `is_object_v<V>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return invoke(std::forward<F>(f), *$val$);
```

:::
