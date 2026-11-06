// include/beman/specgen/diff/sequence.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — the shortest edit script between two sequences, for the wording
// diff (docs/plans/wording-diff.md). Myers' greedy O((N + M)·D) algorithm, the
// one git runs by default, written once over any two random-access ranges and
// an equality predicate: the structural matcher above this hands it words,
// code tokens, lines and paragraphs, never a whole document, which is what
// keeps a shortest script a readable one.
//
// Header-only and Clang-free (Tier A), like parse/: the IR is the thing being
// compared, and nothing downstream of the IR links Clang.

#ifndef BEMAN_SPECGEN_DIFF_SEQUENCE_HPP
#define BEMAN_SPECGEN_DIFF_SEQUENCE_HPP

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

#include <beman/specgen/foundation/enumeration.hpp>

namespace beman::specgen::diff {

// One step of an edit script. `Keep` pairs `a[i]` with `b[j]`; `Delete`
// drops `a[i]`; `Insert` adds `b[j]`. Together, walking a script left to right
// reads out `a` through its `Keep`s and `Delete`s and `b` through its `Keep`s
// and `Insert`s, each in order.
enum class Op { Keep, Delete, Insert };

} // namespace beman::specgen::diff

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::diff::Op> = std::array{
    beman::specgen::diff::Op::Keep,
    beman::specgen::diff::Op::Delete,
    beman::specgen::diff::Op::Insert,
};

namespace beman::specgen::diff {

// `a` is the index into the first sequence and `b` into the second. For a
// `Keep` both name the paired elements; for a `Delete`, `a` names the dropped
// element and `b` the position in the second sequence it would have reached;
// for an `Insert`, `b` names the added element and `a` the position in the
// first sequence it goes before. Carrying both positions on every edit is
// what lets a consumer slice either sequence from any edit without replaying
// the script.
struct Edit {
    Op          op = Op::Keep;
    std::size_t a  = 0;
    std::size_t b  = 0;

    friend bool operator==(const Edit&, const Edit&) = default;
};

namespace sequence_detail {

// The furthest-reaching x on each diagonal k, for k in [-max, max]: Myers'
// `V`, stored with its origin shifted so the diagonal index can be negative.
class Frontier {
  public:
    explicit Frontier(std::ptrdiff_t max) : max_(max), x_(static_cast<std::size_t>(2 * max + 1), 0) {}

    std::size_t& operator[](std::ptrdiff_t k) { return x_[static_cast<std::size_t>(k + max_)]; }
    std::size_t  operator[](std::ptrdiff_t k) const { return x_[static_cast<std::size_t>(k + max_)]; }

  private:
    std::ptrdiff_t           max_;
    std::vector<std::size_t> x_;
};

// Whether the step onto diagonal `k` at depth `d` comes from the diagonal
// above (an insertion, x unchanged) rather than from the left (a deletion,
// x + 1). Myers' own tie-break, which favours the deletion, so that a
// replacement reads as its removal followed by its addition -- the order a
// `\changed{old}{new}` or a `[old]{.rm}[new]{.add}` wants.
inline bool steps_down(const Frontier& v, std::ptrdiff_t d, std::ptrdiff_t k) {
    return k == -d || (k != d && v[k - 1] < v[k + 1]);
}

} // namespace sequence_detail

// The shortest edit script turning `a` into `b` under `eq`, as `Keep`,
// `Delete` and `Insert` steps in order. Empty inputs give an empty script;
// equal inputs give one `Keep` per element.
template <std::ranges::random_access_range A, std::ranges::random_access_range B, class Eq = std::ranges::equal_to>
    requires std::ranges::sized_range<A> && std::ranges::sized_range<B> &&
             std::predicate<Eq&, std::ranges::range_reference_t<const A>, std::ranges::range_reference_t<const B>>
std::vector<Edit> edit_script(const A& a, const B& b, Eq eq = {}) {
    using sequence_detail::Frontier;
    using sequence_detail::steps_down;

    const std::size_t    n   = std::ranges::size(a);
    const std::size_t    m   = std::ranges::size(b);
    const std::ptrdiff_t max = static_cast<std::ptrdiff_t>(n + m);

    // The frontier after each depth, kept so the path can be read back once
    // the end is reached: O((N + M)·D) space, which the inputs this is built
    // for -- the words of a paragraph, the lines of a synopsis -- never make
    // noticeable.
    std::vector<Frontier> trace;
    Frontier              v(max);
    std::ptrdiff_t        depth = -1;

    // substrate generic algorithm: Myers' forward pass is the primitive
    // itself -- each depth extends every reachable diagonal by one edit and
    // then slides along its matches, and no smaller algorithm composes into
    // it.
    for (std::ptrdiff_t d = 0; d <= max && depth < 0; ++d) {
        // substrate generic algorithm: the diagonals of one depth, stepped by
        // two because an edit moves the walk from one parity to the other.
        for (std::ptrdiff_t k = -d; k <= d; k += 2) {
            std::size_t x = steps_down(v, d, k) ? v[k + 1] : v[k - 1] + 1;
            std::size_t y = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(x) - k);
            // substrate generic algorithm: the snake -- slide along the
            // diagonal while the two sequences agree.
            while (x < n && y < m && eq(a[static_cast<std::ptrdiff_t>(x)], b[static_cast<std::ptrdiff_t>(y)])) {
                ++x;
                ++y;
            }
            v[k] = x;
            if (x >= n && y >= m) {
                depth = d;
                break;
            }
        }
        trace.push_back(v);
    }

    // Read the path back from the end, one depth at a time, emitting in
    // reverse; the final reversal puts the script in order.
    std::vector<Edit> script;
    std::size_t       x = n;
    std::size_t       y = m;
    // substrate generic algorithm: the backtrack over the saved frontiers is
    // the second half of the same primitive -- each step is one edit plus the
    // snake that preceded it.
    for (std::ptrdiff_t d = depth; d > 0; --d) {
        const Frontier&      prev   = trace[static_cast<std::size_t>(d - 1)];
        const std::ptrdiff_t k      = static_cast<std::ptrdiff_t>(x) - static_cast<std::ptrdiff_t>(y);
        const std::ptrdiff_t prev_k = steps_down(prev, d, k) ? k + 1 : k - 1;
        const std::size_t    prev_x = prev[prev_k];
        const std::size_t    prev_y = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(prev_x) - prev_k);
        // substrate generic algorithm: the snake again, read backwards.
        while (x > prev_x && y > prev_y) {
            --x;
            --y;
            script.push_back(Edit{Op::Keep, x, y});
        }
        if (x == prev_x)
            script.push_back(Edit{Op::Insert, x, prev_y});
        else
            script.push_back(Edit{Op::Delete, prev_x, y});
        x = prev_x;
        y = prev_y;
    }
    // The depth-0 snake from the origin, if any.
    // substrate generic algorithm: the same backwards snake, at the origin.
    while (x > 0 && y > 0) {
        --x;
        --y;
        script.push_back(Edit{Op::Keep, x, y});
    }
    std::ranges::reverse(script);
    return script;
}

// git's cleanup, for readability rather than minimality: a run of at most
// `island` kept elements wedged between two changes joins them, so that
// `[a]{.rm}[b]{.add} x [c]{.rm}[d]{.add}` becomes one removal and one
// addition. Each change run is then normalized to its deletions followed by
// its insertions, which is the order a replacement is read in. `island` of 0
// only normalizes. The result is no longer a shortest script, by exactly the
// islands it absorbed.
inline std::vector<Edit> join_islands(std::vector<Edit> script, std::size_t island) {
    // Runs of kept and of changed edits, alternating.
    const auto runs =
        script |
        std::views::chunk_by([](const Edit& x, const Edit& y) { return (x.op == Op::Keep) == (y.op == Op::Keep); }) |
        std::ranges::to<std::vector<std::vector<Edit>>>();

    // An island is a kept run short enough, with a change on both sides.
    const auto is_island = [&](std::size_t i) {
        return runs[i].front().op == Op::Keep && runs[i].size() <= island && i > 0 && i + 1 < runs.size();
    };

    // Absorbed islands become their own deletion and insertion, so a change
    // run spanning an island is one run; then each change run is sorted
    // deletions first. The sort is stable, so the elements of each sequence
    // keep their order.
    std::vector<Edit> out;
    out.reserve(script.size());
    std::vector<Edit> change;
    const auto        flush = [&] {
        std::ranges::stable_sort(change, {}, [](const Edit& e) { return e.op == Op::Insert; });
        out.append_range(change);
        change.clear();
    };
    // substrate generic algorithm: a scatter -- each run either joins the open
    // change run or closes it, which is a fold whose accumulator is two
    // containers.
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const std::vector<Edit>& run = runs[i];
        if (run.front().op != Op::Keep) {
            change.append_range(run);
        } else if (is_island(i)) {
            change.append_range(run | std::views::transform([](const Edit& e) { return Edit{Op::Delete, e.a, e.b}; }));
            change.append_range(run | std::views::transform([](const Edit& e) { return Edit{Op::Insert, e.a, e.b}; }));
        } else {
            flush();
            out.append_range(run);
        }
    }
    flush();
    return out;
}

} // namespace beman::specgen::diff

#endif // BEMAN_SPECGEN_DIFF_SEQUENCE_HPP
