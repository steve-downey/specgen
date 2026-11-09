// tests/split_inline/cases/members/input.hpp                      -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Every member shape the plan's spike covered (docs/plans/split-inline.md),
// in one hermetic header: a constrained class template with a defaulted
// parameter, constructors with mem-initializers, a destructor, a conversion
// function, operators, a static and a virtual member, member templates with
// their own constraints, a partial and an explicit specialization, a nested
// class whose members return nested types, hidden friends, and defaulted and
// deleted members. The `std` declarations are local stand-ins.

#ifndef BEMAN_SPECGEN_SPLIT_INLINE_MEMBERS_INPUT_HPP
#define BEMAN_SPECGEN_SPLIT_INLINE_MEMBERS_INPUT_HPP

namespace std {
template <class T>
constexpr bool is_copy_constructible_v = true;
template <class T, class U>
constexpr bool is_convertible_v = true;
} // namespace std

namespace demo::inner {
inline namespace v1 {

struct nullopt_t {
    explicit constexpr nullopt_t(int) {}
};

template <class T, class Alloc = int>
    requires std::is_copy_constructible_v<T>
class optional {
  public:
    using value_type = T;
    struct iterator {
        T*        p_;
        iterator& operator++() {
            ++p_;
            return *this;
        }
        T& operator*() const { return *p_; }
    };

    // \ref{optional.ctor}, constructors
    //! \effects Constructs a disengaged optional.
    constexpr optional() noexcept : engaged_(false), value_() {}
    /// Doxygen on a converting constructor; stays with the declaration.
    explicit constexpr optional(const T& v) : engaged_(true), value_(v) {}
    template <class U = T>
        requires std::is_convertible_v<U, T>
    constexpr optional(U&& u) : engaged_(true), value_(static_cast<T>(u)) {}
    optional(const optional&) = default;
    optional(optional&&)      = delete;

    // \ref{optional.dtor}, destructor
    ~optional() { reset(); }

    // \ref{optional.observe}, observers
    [[nodiscard]] constexpr bool has_value() const noexcept { return engaged_; }
    constexpr explicit           operator bool() const noexcept { return engaged_; }
    iterator                     begin() { return iterator{&value_}; }
    optional&                    self() { return *this; }
    static constexpr int         max_size(int hint = 42, bool strict = false) { return hint + (strict ? 0 : 1); }
    virtual void                 poke() {}
    friend bool                  operator==(const optional& a, const optional& b) { return a.engaged_ == b.engaged_; }
    template <class U>
    friend bool operator==(const optional& a, const U& b) {
        return a.engaged_ && a.value_ == b;
    }

    // \ref{optional.mod}, modifiers
    void reset() noexcept { engaged_ = false; }
    template <class... Args>
    T& emplace(Args&&... args)
        requires std::is_copy_constructible_v<T>
    {
        engaged_ = true;
        value_   = T(static_cast<Args&&>(args)...);
        return value_;
    }

  private:
    bool engaged_;
    T    value_;
};

template <class T, class A>
class optional<T*, A> {
  public:
    bool has_value() const { return p_ != nullptr; }

  private:
    T* p_ = nullptr;
};

template <>
class optional<char, char> {
  public:
    int get() const { return 7; }
};

class widget {
  public:
    widget() : value_(0) {}
    explicit widget(int value) : value_(value) {}
    bool         empty() const { return value_ == 0; }
    void         bump() { ++value_; }
    virtual void poke() {}

  private:
    int value_ = 0;
};

class gadget : public widget {
  public:
    void poke() override { bump(); }
};

} // namespace v1
} // namespace demo::inner

#endif // BEMAN_SPECGEN_SPLIT_INLINE_MEMBERS_INPUT_HPP
