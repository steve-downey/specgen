::: wording

## Monadic operations [optional.ref.monadic]{- .sref} {-}

```cpp
template<class F>
  constexpr auto and_then(F&& f) const;
```

[#]{.pnum} Let `U` be `invoke_result_t<F, T&>`.

[#]{.pnum} *Mandates*: [`remove_cvref_t<U>`]{.rm}[`U`]{.add} is a specialization of `optional`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
@[using U = invoke_result_t<F, T&>;]{.add}@

if (has_value()) {
  return invoke(@[std::]{.rm}@forward<F>(f), *$val$);
} else {
  return remove_cvref_t<U>();
}
```

```cpp
template<class F>
  constexpr optional<@[remove_cv_t<]{.rm}@invoke_result_t<F, T&>> @[>]{.rm}@ transform(F&& f) const;
```

[#]{.pnum} Let `U` be [`remove_cv_t<invoke_result_t<F, T&>>`]{.rm}[`invoke_result_t<F, T&>`]{.add}.

[#]{.pnum} *Mandates*: `U` is a valid contained type for `optional`. The declaration

```cpp
U u(invoke(std::forward<F>(f), *$val$));
```

is well-formed for some invented variable `u`.

::: note

There is no requirement that `U` is movable([dcl.init.general]{- .sref}).

:::

[#]{.pnum} *Returns*: If `*this` contains a value, an `optional<U>` object whose contained value is direct-non-list-initialized with `invoke(std::forward<F>(f), *$val$)`; otherwise, `optional<U>()`.

```cpp
template<class F>
  constexpr optional or_else(F&& f) const;
```

[#]{.pnum} *Constraints*: `F` models [`\tcode{\libconcept{invocable}}`{=latex}]{.rm}[`invocable`]{.add}.

[#]{.pnum} *Mandates*: `is_same_v<remove_cvref_t<invoke_result_t<F>>, optional>` is `true`.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
if (has_value()) {
  return *@[*val*]{.rm}@@[this]{.add}@;
} else {
  return @[std::]{.rm}@forward<F>(f)();
}
```

:::
