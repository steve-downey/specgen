::: wording

## Non-owning wrapper [func.wrap.ref](){.sref} {-}

### General [func.wrap.ref.general](){.sref} {-}

[#]{.pnum} The header provides partial specializations of `function_ref` for each combination of the possible replacements of the placeholders *cv* and *noex* where:

- [#.#]{.pnum} *cv* is either const or empty, and
- [#.#]{.pnum} *noex* is either `true` or `false`.

### Class template `function_ref` [func.wrap.ref.class](){.sref} {-}

```cpp
namespace std {
  template<class R, class... ArgTypes>
  class function_ref<R(ArgTypes...) $cv$ noexcept($noex$)> {
  public:
    // @[func.wrap.ref.ctor](){.sref}@, constructors and assignment operators
    template<class F> function_ref(F*) noexcept;
    template<class F> constexpr function_ref(F&&) noexcept;
    template<auto c, class F> constexpr function_ref(constant_wrapper<c, F>) noexcept;
    template<auto c, class F, class U>
      constexpr function_ref(constant_wrapper<c, F>, U&&) noexcept;
    template<auto c, class F, class T>
      constexpr function_ref(constant_wrapper<c, F>, $cv$ T*) noexcept;

    constexpr function_ref(const function_ref&) noexcept = default;
    constexpr function_ref& operator=(const function_ref&) noexcept = default;
    template<class T> function_ref& operator=(T) = delete;

    // @[func.wrap.ref.inv](){.sref}@, invocation
    R operator()(ArgTypes...) const noexcept($noex$);

  private:
    template<class... T>
      static constexpr bool $is-invocable-using$ = $see below$;             // exposition only

    R (*$thunk-ptr$)($BoundEntityType$, ArgTypes&&...) noexcept($noex$);      // exposition only
    $BoundEntityType$ $bound-entity$;                                       // exposition only
  };

  // @[func.wrap.ref.deduct](){.sref}@, deduction guides
  template<class F>
    function_ref(F*) -> function_ref<F>;
  template<auto c, class F0>
    function_ref(constant_wrapper<c, F0>) -> function_ref<$see below$>;
  template<auto c, class F, class T>
    function_ref(constant_wrapper<c, F>, T&&) -> function_ref<$see below$>;
}
```

[#]{.pnum} An object of class `function_ref<R(Args...) $cv$ noexcept($noex$)>` stores a pointer to function `$thunk-ptr$` and an object `$bound-entity$`. The object `$bound-entity$` has an unspecified trivially copyable type `$BoundEntityType$`, that models `copyable` and is capable of storing a pointer to object value or a pointer to function value. The type of `$thunk-ptr$` is `R(*)($BoundEntityType$, Args&&...) noexcept($noex$)`.

[#]{.pnum} Each specialization of `function_ref` is a trivially copyable type ([term.trivially.copyable.type](){.sref}) that models `copyable`.

[#]{.pnum} Within subclause&nbsp;[func.wrap.ref](){.sref}, `$call-args$` is an argument pack with elements such that

```cpp
decltype(($call-args$))...
```

denote `ArgTypes&&...` respectively.

### Constructors and assignment operators [func.wrap.ref.ctor](){.sref} {-}

```cpp
template<class... T>
  static constexpr bool $is-invocable-using$ = $see below$;
```

[#]{.pnum} If *noex* is `true`, `$is-invocable-using$<T...>` is equal to:

```cpp
is_nothrow_invocable_r_v<R, T..., ArgTypes...>
```

Otherwise, `$is-invocable-using$<T...>` is equal to:

```cpp
is_invocable_r_v<R, T..., ArgTypes...>
```

```cpp
template<class F>
  static constexpr bool $is-convertible-from-specialization$ = $see below$;
```

[#]{.pnum} If `F` denotes a specialization `function_ref<R(Args...) $cv2$ noexcept($noex2$)>` for some placeholders *cv2* and *noex2*, `$is-convertible-from-specialization$<F>` is equal to:

```cpp
is_convertible_v<R(&)(Args...) noexcept($noex2$), R(&)(Args...) noexcept($noex$)> &&
is_convertible_v<int $cv$&, int $cv2$&>.
```

Otherwise, `$is-convertible-from-specialization$<F>` is `false`.

```cpp
template<class F> function_ref(F* f) noexcept;
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_function_v<F>` is `true`, and
- [#.#]{.pnum} `$is-invocable-using$<F>` is `true`.

[#]{.pnum} *Preconditions*: `f` is not a null pointer.

[#]{.pnum} *Effects*: Initializes `$bound-entity$` with `f`, and `$thunk-ptr$` with the address of a function `$thunk$` such that `$thunk$($bound-entity$, $call-args$...)` is expression-equivalent ([defns.expression.equivalent](){.sref}) to `invoke_r<R>(f, $call-args$...)`.

```cpp
template<class F> constexpr function_ref(F&& f) noexcept;
```

[#]{.pnum} Let `T` be `remove_reference_t<F>`.

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `remove_cvref_t<F>` is not the same type as `function_ref`,
- [#.#]{.pnum} `is_member_pointer_v<T>` is `false`, and
- [#.#]{.pnum} `$is-invocable-using$<$cv$ T&>` is `true`.

[#]{.pnum} *Effects*: If `$is-convertible-from-specialization$<remove_cv_t<T>>` is `false`, initializes `$bound-entity$` with `addressof(f)`, and `$thunk-ptr$` with the address of a function `$thunk$` such that `$thunk$($bound-entity$, $call-args$...)` is expression-equivalent ([defns.expression.equivalent](){.sref}) to `invoke_r<R>(static_cast<$cv$ T&>(f), $call-args$...)`. Otherwise, initializes `$bound-entity$` with the value of `f.$bound-entity$` and `$thunk-ptr$` with the value of `f.$thunk-ptr$`.

[#]{.pnum} *Remarks*: If `remove_cvref_t<F>` is a specialization of `function_ref`, an implementation may initialize `$bound-entity$` with the value of `f.$bound-entity$` and `$thunk-ptr$` with the value of `f.$thunk-ptr$`.

::: example

```cpp
void f1(std::string);
void f2(std::string);

function_ref<void(std::string)> r1(&f1);
function_ref<void(std::string&&)> r2(r1);
r2("");                 // \tcode{f1} is invoked
r1 = &f2;
r2("");                 // it is unspecified if \tcode{f1} or \tcode{f2} is invoked
```

:::

```cpp
template<auto c, class F> constexpr function_ref(constant_wrapper<c, F> f) noexcept;
```

[#]{.pnum} *Constraints*: `$is-invocable-using$<const F&>` is `true`.

[#]{.pnum} *Mandates*:

- [#.#]{.pnum} If `is_pointer_v<F> || is_member_pointer_v<F>` is `true`, then `f.value != nullptr` is `true`, and
- [#.#]{.pnum} if `ArgTypes` is not an empty pack and all types in `remove_cvref_t<ArgTypes>...` satisfy `$constexpr-param$` then `constant_wrapper<$INVOKE$(f.value, remove_cvref_t<ArgTypes>::value...)>` is not a valid type.

[#]{.pnum} *Effects*: Initializes `$bound-entity$` with a pointer to an unspecified object or null pointer value, and `$thunk-ptr$` with the address of a function `$thunk$` such that `$thunk$($bound-entity$, $call-args$...)` is expression-equivalent ([defns.expression.equivalent](){.sref}) to `invoke_r<R>(f.value, $call-args$...)`.

```cpp
template<auto c, class F, class U>
  constexpr function_ref(constant_wrapper<c, F> f, U&& obj) noexcept;
```

[#]{.pnum} Let `T` be `remove_reference_t<U>`.

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_rvalue_reference_v<U&&>` is `false`, and
- [#.#]{.pnum} `$is-invocable-using$<const F&, $cv$ T&>` is `true`.

[#]{.pnum} *Mandates*: If `is_pointer_v<F> || is_member_pointer_v<F>` is `true`, then `f.value != nullptr` is `true`.

[#]{.pnum} *Effects*: Initializes `$bound-entity$` with `addressof(obj)`, and `$thunk-ptr$` with the address of a function `$thunk$` such that `$thunk$($bound-entity$, $call-args$...)` is expression-equivalent ([defns.expression.equivalent](){.sref}) to `invoke_r<R>(f.value, static_cast<$cv$ T&>(obj), $call-args$...)`.

```cpp
template<auto c, class F, class T>
  constexpr function_ref(constant_wrapper<c, F> f, $cv$ T* obj) noexcept;
```

[#]{.pnum} *Constraints*: `$is-invocable-using$<const F&, $cv$ T*>` is `true`.

[#]{.pnum} *Mandates*: If `is_pointer_v<F> || is_member_pointer_v<F>` is `true`, then `f.value != nullptr` is `true`.

[#]{.pnum} *Preconditions*: If `is_member_pointer_v<F>` is `true`, `obj` is not a null pointer.

[#]{.pnum} *Effects*: Initializes `$bound-entity$` with `obj`, and `$thunk-ptr$` with the address of a function `$thunk$` such that `$thunk$($bound-entity$, $call-args$...)` is expression-equivalent ([defns.expression.equivalent](){.sref}) to `invoke_r<R>(f.value, obj, $call-args$...)`.

```cpp
template<class T> function_ref& operator=(T) = delete;
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `$is-convertible-from-specialization$<T>` is `false`,
- [#.#]{.pnum} `is_pointer_v<T>` is `false`, and
- [#.#]{.pnum} `T` is not a specialization of `constant_wrapper`.

### Invocation [func.wrap.ref.inv](){.sref} {-}

```cpp
R operator()(ArgTypes... args) const noexcept($noex$);
```

[#]{.pnum} *Effects*: Equivalent to: `return $thunk-ptr$($bound-entity$, std::forward<ArgTypes>(args)...);`

### Deduction guides [func.wrap.ref.deduct](){.sref} {-}

```cpp
template<class F>
  function_ref(F*) -> function_ref<F>;
```

[#]{.pnum} *Constraints*: `is_function_v<F>` is `true`.

```cpp
template<auto c, class F0>
  function_ref(constant_wrapper<c, F0>) -> function_ref<$see below$>;
```

[#]{.pnum} Let `F` be `remove_pointer_t<F0>`.

[#]{.pnum} *Constraints*: `is_function_v<F>` is `true`.

[#]{.pnum} *Remarks*: The deduced type is `function_ref<F>`.

```cpp
template<auto c, class F, class T>
  function_ref(constant_wrapper<c, F>, T&&) -> function_ref<$see below$>;
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `F` is of the form `R(G::*)(A...) $cv$ &@~_opt_~@ noexcept(E)` for a type `G`, or
- [#.#]{.pnum} `F` is of the form `M G::*` for a type `G` and an object type `M`, in which case let `R` be `invoke_result_t<F, T&>`, `A...` be an empty pack, and `E` be `true`, or
- [#.#]{.pnum} `F` is of the form `R(*)(G, A...) noexcept(E)` for a type `G`.

[#]{.pnum} *Remarks*: The deduced type is `function_ref<R(A...) noexcept(E)>`.

:::
