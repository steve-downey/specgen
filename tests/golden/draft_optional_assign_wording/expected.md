::: wording

## Assignment [optional.assign]{- .sref} {-}

```cpp
constexpr optional<T>& operator=(nullopt_t) noexcept;
```

[#]{.pnum} *Effects*: If `*this` contains a value, calls `$val$.T::~T()` to destroy the contained value; otherwise no effect.

[#]{.pnum} *Postconditions*: `*this` does not contain a value.

[#]{.pnum} *Returns*: `*this`.

```cpp
constexpr optional<T>& operator=(const optional& rhs);
```

[#]{.pnum} *Effects*: See [Table \[optional.assign.copy\]](#optional.assign.copy).

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `rhs.$val$` to `$val$` | direct-non-list-initializes `$val$` with `rhs.$val$` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(const optional&)` effects]{#optional.assign.copy}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s copy constructor, no effect. If an exception is thrown during the call to `T`'s copy assignment, the state of its contained value is as defined by the exception safety guarantee of `T`'s copy assignment. This operator is defined as deleted unless `is_copy_constructible_v<T>` is `true` and `is_copy_assignable_v<T>` is `true`. If `is_trivially_copy_constructible_v<T> &&` `is_trivially_copy_assignable_v<T> &&` `is_trivially_destructible_v<T>` is `true`, this assignment operator is trivial.

```cpp
constexpr optional& operator=(optional&& rhs) noexcept($see below$);
```

[#]{.pnum} *Constraints*: `is_move_constructible_v<T>` is `true` and `is_move_assignable_v<T>` is `true`.

[#]{.pnum} *Effects*: See [Table \[optional.assign.move\]](#optional.assign.move). The result of the expression `rhs.has_value()` remains unchanged.

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `std::move(rhs.$val$)` to `$val$` | direct-non-list-initializes `$val$` with `std::move(rhs.$val$)` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(optional&&)` effects]{#optional.assign.move}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: The exception specification is equivalent to:

```cpp
is_nothrow_move_assignable_v<T> && is_nothrow_move_constructible_v<T>
```

[#]{.pnum} If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s move constructor, the state of `rhs.$val$` is determined by the exception safety guarantee of `T`'s move constructor. If an exception is thrown during the call to `T`'s move assignment, the states of `$val$` and `rhs.$val$` are determined by the exception safety guarantee of `T`'s move assignment. If `is_trivially_move_constructible_v<T> &&` `is_trivially_move_assignable_v<T> &&` `is_trivially_destructible_v<T>` is `true`, this assignment operator is trivial.

```cpp
template<class U = remove_cv_t<T>> constexpr optional& operator=(U&& v);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_same_v<remove_cvref_t<U>, optional>` is `false`,
- [#.#]{.pnum} `conjunction_v<is_scalar<T>, is_same<T, decay_t<U>>>` is `false`,
- [#.#]{.pnum} `is_constructible_v<T, U>` is `true`, and
- [#.#]{.pnum} `is_assignable_v<T&, U>` is `true`.

[#]{.pnum} *Effects*: If `*this` contains a value, assigns `std::forward<U>(v)` to `$val$`; otherwise direct-non-list-initializes `$val$` with `std::forward<U>(v)`.

[#]{.pnum} *Postconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s constructor, the state of `v` is determined by the exception safety guarantee of `T`'s constructor. If an exception is thrown during the call to `T`'s assignment, the states of `$val$` and `v` are determined by the exception safety guarantee of `T`'s assignment.

```cpp
template<class U> constexpr optional<T>& operator=(const optional<U>& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, const U&>` is `true`,
- [#.#]{.pnum} `is_assignable_v<T&, const U&>` is `true`,
- [#.#]{.pnum} `$converts-from-any-cvref$<T, optional<U>>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&>` is `false`, and
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&&>` is `false`.

[#]{.pnum} *Effects*: See [Table \[optional.assign.copy.templ\]](#optional.assign.copy.templ).

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `rhs.operator*()` to `$val$` | direct-non-list-initializes `$val$` with `rhs.operator*()` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(const optional<U>&)` effects]{#optional.assign.copy.templ}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s constructor, the state of `rhs.$val$` is determined by the exception safety guarantee of `T`'s constructor. If an exception is thrown during the call to `T`'s assignment, the states of `$val$` and `rhs.$val$` are determined by the exception safety guarantee of `T`'s assignment.

```cpp
template<class U> constexpr optional<T>& operator=(optional<U>&& rhs);
```

[#]{.pnum} *Constraints*:

- [#.#]{.pnum} `is_constructible_v<T, U>` is `true`,
- [#.#]{.pnum} `is_assignable_v<T&, U>` is `true`,
- [#.#]{.pnum} `$converts-from-any-cvref$<T, optional<U>>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, optional<U>&&>` is `false`,
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&>` is `false`, and
- [#.#]{.pnum} `is_assignable_v<T&, const optional<U>&&>` is `false`.

[#]{.pnum} *Effects*: See [Table \[optional.assign.move.templ\]](#optional.assign.move.templ). The result of the expression `rhs.has_value()` remains unchanged.

| | `*this` contains a value | `*this` does not contain a value |
|---|---|---|
| **`rhs` contains a value** | assigns `std::move(rhs).operator*()` to `$val$` | direct-non-list-initializes `$val$` with `std::move(rhs).operator*()` |
| **`rhs` does not contain a value** | destroys the contained value by calling `$val$.T::~T()` | no effect |
: [`optional::operator=(optional<U>&&)` effects]{#optional.assign.move.templ}

[#]{.pnum} *Postconditions*: `rhs.has_value() == this->has_value()`.

[#]{.pnum} *Returns*: `*this`.

[#]{.pnum} *Remarks*: If any exception is thrown, the result of the expression `this->has_value()` remains unchanged. If an exception is thrown during the call to `T`'s constructor, the state of `rhs.$val$` is determined by the exception safety guarantee of `T`'s constructor. If an exception is thrown during the call to `T`'s assignment, the states of `$val$` and `rhs.$val$` are determined by the exception safety guarantee of `T`'s assignment.

```cpp
template<class... Args> constexpr T& emplace(Args&&... args);
```

[#]{.pnum} *Constraints*: `is_constructible_v<T, Args...>` is `true`.

[#]{.pnum} *Effects*: Calls `*this = nullopt`. Then direct-non-list-initializes `$val$` with `std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: `$val$`.

[#]{.pnum} *Throws*: Any exception thrown by the selected constructor of `T`.

[#]{.pnum} *Remarks*: If an exception is thrown during the call to `T`'s constructor, `*this` does not contain a value, and the previous `$val$` (if any) has been destroyed.

```cpp
template<class U, class... Args> constexpr T& emplace(initializer_list<U> il, Args&&... args);
```

[#]{.pnum} *Constraints*: `is_constructible_v<T, initializer_list<U>&, Args...>` is `true`.

[#]{.pnum} *Effects*: Calls `*this = nullopt`. Then direct-non-list-initializes `$val$` with `il, std::forward<Args>(args)...`.

[#]{.pnum} *Postconditions*: `*this` contains a value.

[#]{.pnum} *Returns*: `$val$`.

[#]{.pnum} *Throws*: Any exception thrown by the selected constructor of `T`.

[#]{.pnum} *Remarks*: If an exception is thrown during the call to `T`'s constructor, `*this` does not contain a value, and the previous `$val$` (if any) has been destroyed.

:::
