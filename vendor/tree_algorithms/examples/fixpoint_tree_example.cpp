// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The self-contained "20 lines on Godbolt" demo the article opens with.
// Deliberately does NOT include any beman::tree_algorithms header: this
// file IS the compact story, buildable by pasting the whole thing into
// Compiler Explorer with any C++23 toolchain. The library headers ship
// the full versions of everything sketched here.

// ec8e7f28-10c3-43ea-bfa9-a54f4c63ef9c
#include <utility>
#include <variant>

// Box<A>: a small owning pointer with deep-copy value semantics. It
// exists to break a cycle in the type system; nothing tree-specific.
template <typename A>
struct Box {
    A* ptr = nullptr;

    constexpr Box() = default;
    constexpr explicit Box(A* p) : ptr(p) {}
    constexpr Box(const Box& other) : ptr(other.ptr ? new A(*other.ptr) : nullptr) {}
    constexpr Box(Box&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {}
    constexpr auto operator=(Box other) noexcept -> Box& {
        std::swap(ptr, other.ptr);
        return *this;
    }
    constexpr ~Box() { delete ptr; }

    constexpr auto operator*() const -> A& { return *ptr; }
    constexpr auto operator->() const -> A* { return ptr; }
};

template <typename A, typename... Args>
constexpr auto make_box(Args&&... args) -> Box<A> {
    return Box<A>(new A(std::forward<Args>(args)...));
}
// ec8e7f28-10c3-43ea-bfa9-a54f4c63ef9c end

// 3959d7ab-f1fa-4fd7-b2b9-886a1f7610a2
// The entire type system for an expression tree. ExprF never refers to
// itself; A fills the recursive positions. Fix ties the knot.
template <typename A>
struct Const {
    int val;
};
template <typename A>
struct Add {
    Box<A> left, right;
};
template <typename A>
struct Mul {
    Box<A> left, right;
};

template <typename A>
using ExprF = std::variant<Const<A>, Add<A>, Mul<A>>;

template <template <typename> class F>
struct Fix {
    F<Fix<F>> inner;
};

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
// 3959d7ab-f1fa-4fd7-b2b9-886a1f7610a2 end

// 02640f07-f080-4eec-bd90-b414fabd4b7b
// Smart constructors hide the wrapping ceremony.
using ExprTree = Fix<ExprF>;

ExprTree const_node(int v) { return Fix<ExprF>{Const<ExprTree>{v}}; }

ExprTree add_node(ExprTree l, ExprTree r) {
    return Fix<ExprF>{Add<ExprTree>{make_box<ExprTree>(std::move(l)), make_box<ExprTree>(std::move(r))}};
}

ExprTree mul_node(ExprTree l, ExprTree r) {
    return Fix<ExprF>{Mul<ExprTree>{make_box<ExprTree>(std::move(l)), make_box<ExprTree>(std::move(r))}};
}
// 02640f07-f080-4eec-bd90-b414fabd4b7b end

#include <print>

// 2526ec45-2e52-4b56-aab0-1034b078a459
int main() {
    ExprTree tree = mul_node(add_node(const_node(1), const_node(2)), const_node(3));

    // One layer only: children are already ints. Not recursive.
    auto eval = overloaded{
        [](const Const<int>& c) { return c.val; },
        [](const Add<int>& a) { return *a.left + *a.right; },
        [](const Mul<int>& m) { return *m.left * *m.right; },
    };

    // The recursion, supplied separately: fold children to ints first,
    // then hand `eval` one layer whose children are already numbers.
    // (The library generalizes this local fold into fold_fix.)
    auto fold_eval = [&eval](this auto&& self, const ExprTree& t) -> int {
        return std::visit(overloaded{
                              [&](const Const<ExprTree>& c) { return eval(Const<int>{c.val}); },
                              [&](const Add<ExprTree>& a) {
                                  return eval(Add<int>{make_box<int>(self(*a.left)), make_box<int>(self(*a.right))});
                              },
                              [&](const Mul<ExprTree>& m) {
                                  return eval(Mul<int>{make_box<int>(self(*m.left)), make_box<int>(self(*m.right))});
                              },
                          },
                          t.inner);
    };

    int result = fold_eval(tree);
    std::println("(1 + 2) * 3 = {}", result); // prints 9
    return result == 9 ? 0 : 1;
}
// 2526ec45-2e52-4b56-aab0-1034b078a459 end
