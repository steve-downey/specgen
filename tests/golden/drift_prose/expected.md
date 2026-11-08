::: wording

## Functions [drift.functions]{- .sref} {-}

```cpp
template<class F>
  void accept(F);
```

[#]{.pnum} Let `VT` be `F`.

[#]{.pnum} *Constraints*: `VT` is not the same type as `int`.

```cpp
template<class T>
  void construct();
```

[#]{.pnum} Let `VT` be `decay_t<T>`.

[#]{.pnum} *Mandates*: `VT` is the same type as `T`.

```cpp
template<class F>
  void invoke(F);
```

[#]{.pnum} Let `VT` be `decay_t<F>`.

[#]{.pnum} *Constraints*: `callable<VT>` is `true`.

:::
