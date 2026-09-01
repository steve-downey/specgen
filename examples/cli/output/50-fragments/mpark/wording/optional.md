```cpp
struct nullopt_t {};
```

```cpp
struct in_place_t {
  explicit in_place_t() = default;
};
```

```cpp
template<class T>
class optional {
public:
  using value_type = T;

  // @[optional.ctor]{- .sref}@, constructors
  constexpr optional() noexcept;
  constexpr optional(nullopt_t) noexcept;

  constexpr optional(const optional& rhs)
    requires is_copy_constructible_v<T> && (!is_trivially_copy_constructible_v<T>);

  template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args)
    requires is_constructible_v<T, Args...>;

  template<class U>
  constexpr explicit(!is_convertible_v<U, T>) optional(const optional<U>& rhs)
    requires is_constructible_v<T, const U&> && is_convertible_v<U, T> &&
             (!is_same_v<T, U>) && (!is_constructible_v<T, optional<U>>);

  // @[optional.assign]{- .sref}@, assignment
  template<class... Args> constexpr T& emplace(Args&&... args);

  // @[optional.observe]{- .sref}@, observers
  constexpr bool has_value() const noexcept;
  constexpr const T& operator*() const&;

  template<class U = remove_cv_t<T>> constexpr remove_cv_t<T> value_or(U&& u) const&;

  // @[optional.monadic]{- .sref}@, monadic operations
  template<class F> constexpr $see below$ transform(F&& f) const&;

  // @[optional.mod]{- .sref}@, modifiers
  constexpr void reset() noexcept;

  friend constexpr bool operator==(const optional& x, const optional& y);

private:
  T $value$;            // exposition only
  bool $engaged$ = false; // exposition only
};
```
