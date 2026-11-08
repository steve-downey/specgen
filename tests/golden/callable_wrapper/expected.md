::: wording

## Callable wrappers [callable]{- .sref} {-}

### General [callable.general]{- .sref} {-}

[#]{.pnum} The header provides callable wrappers for each combination of the placeholders.

[#]{.pnum} The invocation qualifiers determine how the stored target is called.

### Class template callable_wrapper [callable.class]{- .sref} {-}

```cpp
namespace std {
  template<class R, class... ArgTypes>
  class callable_wrapper {
  public:
    // @[callable.ctor]{- .sref}@, constructors

    template<class F> callable_wrapper(F&& f);

  private:
    // @[callable.ctor]{- .sref}@, exposition members

    template<class VT>
    static constexpr bool $is-callable-from$ = $see below$; // exposition only
  };
}
```

[#]{.pnum} The `callable_wrapper` class template provides polymorphic wrappers that generalize the notion of a callable object([func.def]{- .sref}). These wrappers can store, move, and call arbitrary callable objects, given a call signature.

[#]{.pnum} *Recommended practice*: Implementations should avoid the use of dynamically allocated memory for a small contained value.

::: note

Such small-object optimization can only be applied to a type `T` for which `is_nothrow_move_constructible_v<T>` is `true`.

:::

### Construction [callable.ctor]{- .sref} {-}

```cpp
template<class F>
  callable_wrapper(F&& f);
```

[#]{.pnum} *Postconditions*: `*this` has no target object if any of the following hold:

- [#.#]{.pnum} `f` is a null function pointer value, or
- [#.#]{.pnum} `f` is a null member pointer value, or
- [#.#]{.pnum} `f` is an empty callable wrapper.

Otherwise, `*this` has a target object initialized with `f`.

```cpp
template<class VT>
  static constexpr bool $is-callable-from$ = $see below$;
```

[#]{.pnum} If the invocation is nonthrowing, `$is-callable-from$<VT>` is equal to:

```cpp
is_nothrow_invocable_r_v<R, VT, ArgTypes...>
```

Otherwise, `$is-callable-from$<VT>` is equal to:

```cpp
is_invocable_r_v<R, VT, ArgTypes...>
```

::: example

A callable target can be checked as follows:

```cpp
static_assert(is_invocable_r_v<int, int (*)()>);
```

:::

:::
