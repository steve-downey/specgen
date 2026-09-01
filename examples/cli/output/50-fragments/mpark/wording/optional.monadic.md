::: wording

## Monadic operations [optional.monadic]{- .sref} {-}

```cpp
template<class F> constexpr $see below$ transform(F&& f) const&;
```

[#]{.pnum} *Effects*: If `*this` contains a value, returns an optional holding the result of invoking `f` with the contained value; otherwise returns an empty optional.

[#]{.pnum} *Remarks*: The return type is `remove_cvref_t<invoke_result_t<F, const T&>>`.

:::
