::: wording

## Explicit coverage [demo.coverage]{- .sref} {-}

```cpp
template<class F> void accept(F);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `remove_cvref_t<F>` is not a specialization of `in_place_type_t`.

```cpp
namespace std {
  template<class T>
  struct in_place_type_t {};
}
```

### Class move_only_function [demo.coverage.class]{- .sref} {-}

```cpp
namespace std {
  class move_only_function {
  private:
    // @[demo.coverage.ctors]{- .sref}@, exposition members

    template<class VT>
    static constexpr bool $is-callable-from$ = $see below$; // exposition only

  public:
    // @[demo.coverage.ctors]{- .sref}@, constructors

    template<class F>
      requires(!is_same_v<remove_cvref_t<F>, move_only_function>) &&
              (!detail::is_in_place_type<remove_cvref_t<F>>) &&
              ($is-callable-from$<decay_t<F>>)
    constexpr move_only_function(F&& f);

    template<class T> constexpr explicit move_only_function(in_place_type_t<T>);
  };
}
```

[#]{.pnum} The wrapper stores a callable object.

### Construction [demo.coverage.ctors]{- .sref} {-}

```cpp
template<class VT> static constexpr bool $is-callable-from$ = $see below$;
```

[#]{.pnum} The predicate determines whether `VT` can be invoked.

```cpp
template<class F> constexpr move_only_function(F&& f);
```

[#]{.pnum} Let `VT` be `decay_t<F>`.

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `remove_cvref_t<F>` is not the same type as `move_only_function`, and
- [#.#]{.pnum} `remove_cvref_t<F>` is not a specialization of `in_place_type_t`, and
- [#.#]{.pnum} `$is-callable-from$<VT>` is `true`.

```cpp
template<class T> constexpr explicit move_only_function(in_place_type_t<T>);
```

[#]{.pnum} Let `VT` be `decay_t<T>`.

[#]{.pnum} *Mandates*: `VT` is the same type as `T`.

:::
