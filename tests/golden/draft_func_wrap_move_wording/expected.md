::: wording

## Move-only wrapper [func.wrap.move]{- .sref} {-}

### General [func.wrap.move.general]{- .sref} {-}

[#]{.pnum} The header provides partial specializations of `move_only_function` for each combination of the possible replacements of the placeholders *cv*, *ref*, and *noex* where

- [#.#]{.pnum} *cv* is either const or empty,
- [#.#]{.pnum} *ref* is either `&`, `&&`, or empty, and
- [#.#]{.pnum} *noex* is either `true` or `false`.

[#]{.pnum} For each of the possible combinations of the placeholders mentioned above, there is a placeholder *inv-quals* defined as follows:

- [#.#]{.pnum} If *ref* is empty, let *inv-quals* be *cv*`&`,
- [#.#]{.pnum} otherwise, let *inv-quals* be *cv* *ref*.

### Class template `move_only_function` [func.wrap.move.class]{- .sref} {-}

```cpp
namespace std {
  template<class R, class... ArgTypes>
  class move_only_function<R(ArgTypes...) $cv$ $ref$ noexcept($noex$)> {
  public:
    using result_type = R;

    // @[func.wrap.move.ctor]{- .sref}@, constructors, assignments, and destructor
    move_only_function() noexcept;
    move_only_function(nullptr_t) noexcept;
    move_only_function(move_only_function&&) noexcept;
    template<class F> move_only_function(F&&);
    template<class T, class... Args>
      explicit move_only_function(in_place_type_t<T>, Args&&...);
    template<class T, class U, class... Args>
      explicit move_only_function(in_place_type_t<T>, initializer_list<U>, Args&&...);

    move_only_function& operator=(move_only_function&&);
    move_only_function& operator=(nullptr_t) noexcept;
    template<class F> move_only_function& operator=(F&&);

    ~move_only_function();

    // @[func.wrap.move.inv]{- .sref}@, invocation
    explicit operator bool() const noexcept;
    R operator()(ArgTypes...) $cv$ $ref$ noexcept($noex$);

    // @[func.wrap.move.util]{- .sref}@, utility
    void swap(move_only_function&) noexcept;
    friend void swap(move_only_function&, move_only_function&) noexcept;
    friend bool operator==(const move_only_function&, nullptr_t) noexcept;

  private:
    template<class VT>
      static constexpr bool $is-callable-from$ = $see below$;       // \expos
  };
}
```

[#]{.pnum} The `move_only_function` class template provides polymorphic wrappers that generalize the notion of a callable object([func.def]{- .sref}). These wrappers can store, move, and call arbitrary callable objects, given a call signature.

[#]{.pnum} *Recommended practice*: Implementations should avoid the use of dynamically allocated memory for a small contained value.

::: note

Such small-object optimization can only be applied to a type `T` for which `is_nothrow_move_constructible_v<T>` is `true`.

:::

### Constructors, assignments, and destructor [func.wrap.move.ctor]{- .sref} {-}

```cpp
template<class VT>
  static constexpr bool $is-callable-from$ = $see below$;
```

[#]{.pnum} If *noex* is `true`, `$is-callable-from$<VT>` is equal to:

```cpp
is_nothrow_invocable_r_v<R, VT $cv$ $ref$, ArgTypes...> &&
is_nothrow_invocable_r_v<R, VT $inv-quals$, ArgTypes...>
```

Otherwise, `$is-callable-from$<VT>` is equal to:

```cpp
is_invocable_r_v<R, VT $cv$ $ref$, ArgTypes...> &&
is_invocable_r_v<R, VT $inv-quals$, ArgTypes...>
```

```cpp
move_only_function() noexcept;
move_only_function(nullptr_t) noexcept;
```

[#]{.pnum} *Postconditions*: `*this` has no target object.

```cpp
move_only_function(move_only_function&& f) noexcept;
```

[#]{.pnum} *Postconditions*: The target object of `*this` is the target object `f` had before construction, and `f` is in a valid state with an unspecified value.

```cpp
template<class F> move_only_function(F&& f);
```

[#]{.pnum} Let `VT` be `decay_t<F>`.

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `remove_cvref_t<F>` is not the same type as `move_only_function`, and
- [#.#]{.pnum} `remove_cvref_t<F>` is not a specialization of `in_place_type_t`, and
- [#.#]{.pnum} `$is-callable-from$<VT>` is `true`.

[#]{.pnum} *Mandates*: `is_constructible_v<VT, F>` is `true`.

[#]{.pnum} *Preconditions*: `VT` meets the *Cpp17Destructible* requirements, and if `is_move_constructible_v<VT>` is `true`, `VT` meets the *Cpp17MoveConstructible* requirements.

[#]{.pnum} *Postconditions*: `*this` has no target object if any of the following hold:

- [#.#]{.pnum} `f` is a null function pointer value,
- [#.#]{.pnum} `f` is a null member pointer value, or
- [#.#]{.pnum} `remove_cvref_t<F>` is a specialization of the `move_only_function` or `copyable_function` class template, and `f` has no target object.

Otherwise, `*this` has a target object of type `VT` direct-non-list-initialized with `std::forward<F>(f)`.

[#]{.pnum} *Throws*: Any exception thrown by the initialization of the target object. May throw `bad_alloc` unless `VT` is a function pointer or a specialization of `reference_wrapper`.

```cpp
template<class T, class... Args>
  explicit move_only_function(in_place_type_t<T>, Args&&... args);
```

[#]{.pnum} Let `VT` be `decay_t<T>`.

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<VT, Args...>` is `true`, and
- [#.#]{.pnum} `$is-callable-from$<VT>` is `true`.

[#]{.pnum} *Mandates*: `VT` is the same type as `T`.

[#]{.pnum} *Preconditions*: `VT` meets the *Cpp17Destructible* requirements, and if `is_move_constructible_v<VT>` is `true`, `VT` meets the *Cpp17MoveConstructible* requirements.

[#]{.pnum} *Postconditions*: `*this` has a target object of type `VT` direct-non-list-initialized with `std::forward<Args>(args)...`.

[#]{.pnum} *Throws*: Any exception thrown by the initialization of the target object. May throw `bad_alloc` unless `VT` is a function pointer or a specialization of `reference_wrapper`.

```cpp
template<class T, class U, class... Args>
  explicit move_only_function(in_place_type_t<T>, initializer_list<U> ilist, Args&&... args);
```

[#]{.pnum} Let `VT` be `decay_t<T>`.

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<VT, initializer_list<U>&, Args...>` is `true`, and
- [#.#]{.pnum} `$is-callable-from$<VT>` is `true`.

[#]{.pnum} *Mandates*: `VT` is the same type as `T`.

[#]{.pnum} *Preconditions*: `VT` meets the *Cpp17Destructible* requirements, and if `is_move_constructible_v<VT>` is `true`, `VT` meets the *Cpp17MoveConstructible* requirements.

[#]{.pnum} *Postconditions*: `*this` has a target object of type `VT` direct-non-list-initialized with `ilist, std::forward<Args>(args)...`.

[#]{.pnum} *Throws*: Any exception thrown by the initialization of the target object. May throw `bad_alloc` unless `VT` is a function pointer or a specialization of `reference_wrapper`.

```cpp
move_only_function& operator=(move_only_function&& f);
```

[#]{.pnum} *Effects*: Equivalent to: `move_only_function(std::move(f)).swap(*this);`

[#]{.pnum} *Returns*: `*this`.

```cpp
move_only_function& operator=(nullptr_t) noexcept;
```

[#]{.pnum} *Effects*: Destroys the target object of `*this`, if any.

[#]{.pnum} *Returns*: `*this`.

```cpp
template<class F> move_only_function& operator=(F&& f);
```

[#]{.pnum} *Effects*: Equivalent to: `move_only_function(std::forward<F>(f)).swap(*this);`

[#]{.pnum} *Returns*: `*this`.

```cpp
~move_only_function();
```

[#]{.pnum} *Effects*: Destroys the target object of `*this`, if any.

### Invocation [func.wrap.move.inv]{- .sref} {-}

```cpp
explicit operator bool() const noexcept;
```

[#]{.pnum} *Returns*: `true` if `*this` has a target object, otherwise `false`.

```cpp
R operator()(ArgTypes... args) $cv$ $ref$ noexcept($noex$);
```

[#]{.pnum} *Preconditions*: `*this` has a target object.

[#]{.pnum} *Effects*: Equivalent to:

```cpp
return $INVOKE$<R>(static_cast<F $inv-quals$>(f), std::forward<ArgTypes>(args)...);
```

where `f` is an lvalue designating the target object of `*this` and `F` is the type of `f`.

### Utility [func.wrap.move.util]{- .sref} {-}

```cpp
void swap(move_only_function& other) noexcept;
```

[#]{.pnum} *Effects*: Exchanges the target objects of `*this` and `other`.

```cpp
friend void swap(move_only_function& f1, move_only_function& f2) noexcept;
```

[#]{.pnum} *Effects*: Equivalent to `f1.swap(f2)`.

```cpp
friend bool operator==(const move_only_function& f, nullptr_t) noexcept;
```

[#]{.pnum} *Returns*: `true` if `f` has no target object, otherwise `false`.

:::
