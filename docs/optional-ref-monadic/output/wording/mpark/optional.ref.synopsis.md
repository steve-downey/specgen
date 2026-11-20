```cpp
namespace std {
  template<class T>
  class optional<T&> {
  public:
    // @[optional.ref.monadic]{- .sref}@, monadic operations
    template<class F> constexpr auto and_then(F&& f) const;

    template<class F> constexpr optional<invoke_result_t<F, T&>> transform(F&& f) const;

    template<class F> constexpr optional or_else(F&& f) const;

  private:
    T* $val$ = nullptr; // exposition only
  };
}
```
