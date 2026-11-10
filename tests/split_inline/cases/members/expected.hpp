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
    explicit constexpr nullopt_t(int);
};

constexpr nullopt_t::nullopt_t(int) {}

template <class T, class Alloc = int>
    requires std::is_copy_constructible_v<T>
class optional {
  public:
    using value_type = T;
    struct iterator {
        T*        p_;
        iterator& operator++();
        T& operator*() const;
    };

    // \ref{optional.ctor}, constructors
    //! \effects Constructs a disengaged optional.
    constexpr optional() noexcept;
    /// Doxygen on a converting constructor; stays with the declaration.
    explicit constexpr optional(const T& v);
    template <class U = T>
        requires std::is_convertible_v<U, T>
    constexpr optional(U&& u);
    optional(const optional&) = default;
    optional(optional&&)      = delete;

    // \ref{optional.dtor}, destructor
    ~optional();

    // \ref{optional.observe}, observers
    [[nodiscard]] constexpr bool has_value() const noexcept;
    constexpr explicit           operator bool() const noexcept;
    iterator                     begin();
    optional&                    self();
    static constexpr int         max_size(int hint = 42, bool strict = false);
    virtual void                 poke();
    friend bool                  operator==(const optional& a, const optional& b) { return a.engaged_ == b.engaged_; }
    template <class U>
    friend bool operator==(const optional& a, const U& b) {
        return a.engaged_ && a.value_ == b;
    }

    // \ref{optional.mod}, modifiers
    void reset() noexcept;
    template <class... Args>
    T& emplace(Args&&... args)
        requires std::is_copy_constructible_v<T>;

  private:
    bool engaged_;
    T    value_;
};

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
auto optional<T, Alloc>::iterator::operator++() -> iterator& {
    ++p_;
    return *this;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
T& optional<T, Alloc>::iterator::operator*() const {
    return *p_;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr optional<T, Alloc>::optional() noexcept : engaged_(false), value_() {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr optional<T, Alloc>::optional(const T& v) : engaged_(true), value_(v) {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
template <class U>
    requires std::is_convertible_v<U, T>
constexpr optional<T, Alloc>::optional(U&& u) : engaged_(true), value_(static_cast<T>(u)) {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
optional<T, Alloc>::~optional() {
    reset();
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr bool optional<T, Alloc>::has_value() const noexcept {
    return engaged_;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr optional<T, Alloc>::operator bool() const noexcept {
    return engaged_;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
auto optional<T, Alloc>::begin() -> iterator {
    return iterator{&value_};
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
auto optional<T, Alloc>::self() -> optional& {
    return *this;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
constexpr int optional<T, Alloc>::max_size(int hint, bool strict) {
    return hint + (strict ? 0 : 1);
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
void optional<T, Alloc>::poke() {}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
void optional<T, Alloc>::reset() noexcept {
    engaged_ = false;
}

template <class T, class Alloc>
    requires std::is_copy_constructible_v<T>
template <class... Args>
T& optional<T, Alloc>::emplace(Args&&... args)
    requires std::is_copy_constructible_v<T>
{
    engaged_ = true;
    value_   = T(static_cast<Args&&>(args)...);
    return value_;
}

template <class T, class A>
class optional<T*, A> {
  public:
    bool has_value() const;

  private:
    T* p_ = nullptr;
};

template <class T, class A>
bool optional<T*, A>::has_value() const {
    return p_ != nullptr;
}

template <>
class optional<char, char> {
  public:
    int get() const;
};

inline int optional<char, char>::get() const { return 7; }

class widget {
  public:
    widget();
    explicit widget(int value);
    bool         empty() const;
    void         bump();
    virtual void poke();

  private:
    int value_ = 0;
};

inline widget::widget() : value_(0) {}

inline widget::widget(int value) : value_(value) {}

inline bool widget::empty() const { return value_ == 0; }

inline void widget::bump() { ++value_; }

inline void widget::poke() {}

class gadget : public widget {
  public:
    void poke() override;
};

inline void gadget::poke() { bump(); }

} // namespace v1
} // namespace demo::inner

#endif // BEMAN_SPECGEN_SPLIT_INLINE_MEMBERS_INPUT_HPP
