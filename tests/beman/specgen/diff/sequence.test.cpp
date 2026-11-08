// tests/beman/specgen/diff/sequence.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/diff/sequence.hpp>
#include <beman/specgen/diff/sequence.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace beman::specgen::diff;

namespace {

// The second sequence, read back out of a script applied to the first: every
// Keep and Insert contributes one element of `b`. This is the law a script
// has to satisfy before anything else is asked of it.
std::string apply(std::string_view a, std::string_view b, const std::vector<Edit>& script) {
    std::string out;
    for (const Edit& e : script) {
        if (e.op == Op::Keep)
            out += a[e.a];
        else if (e.op == Op::Insert)
            out += b[e.b];
    }
    return out;
}

// The first sequence, read back the same way: every Keep and Delete.
std::string restore(std::string_view a, const std::vector<Edit>& script) {
    std::string out;
    for (const Edit& e : script)
        if (e.op != Op::Insert)
            out += a[e.a];
    return out;
}

// The textbook longest-common-subsequence length, quadratic and obviously
// right, which Myers' script length is checked against.
std::size_t lcs_length(std::string_view a, std::string_view b) {
    std::vector<std::vector<std::size_t>> t(a.size() + 1, std::vector<std::size_t>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j)
            t[i][j] = a[i - 1] == b[j - 1] ? t[i - 1][j - 1] + 1 : std::max(t[i - 1][j], t[i][j - 1]);
    return t[a.size()][b.size()];
}

std::size_t count(const std::vector<Edit>& script, Op op) {
    return static_cast<std::size_t>(std::ranges::count(script, op, &Edit::op));
}

// Every pair of inputs a script is asked for, as a string of the ops.
std::string ops(const std::vector<Edit>& script) {
    std::string out;
    for (const Edit& e : script)
        out += e.op == Op::Keep ? '=' : e.op == Op::Delete ? '-' : '+';
    return out;
}

} // namespace

TEST_CASE("sequence - two empty sequences have an empty script") {
    CHECK(edit_script(std::string_view{}, std::string_view{}).empty());
}

TEST_CASE("sequence - equal sequences keep every element, in order") {
    const std::vector<Edit> script = edit_script(std::string_view{"abc"}, std::string_view{"abc"});
    CHECK(script == std::vector<Edit>{{Op::Keep, 0, 0}, {Op::Keep, 1, 1}, {Op::Keep, 2, 2}});
}

TEST_CASE("sequence - an empty first sequence inserts every element of the second") {
    const std::vector<Edit> script = edit_script(std::string_view{}, std::string_view{"ab"});
    CHECK(script == std::vector<Edit>{{Op::Insert, 0, 0}, {Op::Insert, 0, 1}});
}

TEST_CASE("sequence - an empty second sequence deletes every element of the first") {
    const std::vector<Edit> script = edit_script(std::string_view{"ab"}, std::string_view{});
    CHECK(script == std::vector<Edit>{{Op::Delete, 0, 0}, {Op::Delete, 1, 0}});
}

TEST_CASE("sequence - a replacement reads as its deletion then its insertion") {
    const std::vector<Edit> script = edit_script(std::string_view{"axc"}, std::string_view{"ayc"});
    CHECK(ops(script) == "=-+=");
    CHECK(script[1] == Edit{Op::Delete, 1, 1});
    CHECK(script[2] == Edit{Op::Insert, 2, 1});
}

TEST_CASE("sequence - the script is shortest and applies, over a battery of pairs") {
    const std::vector<std::pair<std::string_view, std::string_view>> pairs = {
        {"ABCABBA", "CBABAC"},
        {"kitten", "sitting"},
        {"", "x"},
        {"x", ""},
        {"same", "same"},
        {"abcdefgh", "hgfedcba"},
        {"aaaa", "aa"},
        {"aa", "aaaa"},
        {"the quick brown fox", "the slow brown dog"},
        {"abc", "xyz"},
    };
    for (const auto& [a, b] : pairs) {
        CAPTURE(a, b);
        const std::vector<Edit> script = edit_script(a, b);
        CHECK(apply(a, b, script) == b);
        CHECK(restore(a, script) == a);
        const std::size_t lcs = lcs_length(a, b);
        CHECK(count(script, Op::Keep) == lcs);
        CHECK(count(script, Op::Delete) + count(script, Op::Insert) == a.size() + b.size() - 2 * lcs);
    }
}

TEST_CASE("sequence - a custom equality predicate is honoured") {
    const std::vector<int> a          = {1, 2, 3};
    const std::vector<int> b          = {11, 22, 33};
    const auto             same_digit = [](int x, int y) { return x % 10 == y % 10; };
    CHECK(ops(edit_script(a, b, same_digit)) == "===");
    CHECK(ops(edit_script(a, b)) == "---+++");
}

TEST_CASE("sequence - join_islands absorbs a short kept run between two changes") {
    // a b c d e  ->  A b C d E : every other letter changed, two one-letter
    // islands between the changes.
    const std::vector<Edit> script = edit_script(std::string_view{"abcde"}, std::string_view{"AbCdE"});
    REQUIRE(ops(script) == "-+=-+=-+");

    const std::vector<Edit> joined = join_islands(script, 1);
    // One change run: the deletions of a, b, c, d, e then the insertions of
    // A, b, C, d, E -- the islands become a deletion and an insertion each.
    CHECK(ops(joined) == "-----+++++");
    CHECK(apply("abcde", "AbCdE", joined) == "AbCdE");
    CHECK(restore("abcde", joined) == "abcde");
}

TEST_CASE("sequence - join_islands leaves a kept run longer than the island alone") {
    const std::vector<Edit> script = edit_script(std::string_view{"abcde"}, std::string_view{"AbcdE"});
    REQUIRE(ops(script) == "-+===-+");
    CHECK(ops(join_islands(script, 2)) == "-+===-+");
    CHECK(ops(join_islands(script, 3)) == "-----+++++");
}

TEST_CASE("sequence - join_islands never absorbs a kept run at either end") {
    const std::vector<Edit> script = edit_script(std::string_view{"xab"}, std::string_view{"yab"});
    REQUIRE(ops(script) == "-+==");
    CHECK(ops(join_islands(script, 5)) == "-+==");
}

TEST_CASE("sequence - join_islands with island 0 only orders a change run deletions first") {
    // Myers already emits a replacement as delete-then-insert; a run built
    // from an insertion followed by a deletion is reordered.
    std::vector<Edit> script = {{Op::Insert, 0, 0}, {Op::Delete, 0, 1}, {Op::Keep, 1, 1}};
    CHECK(join_islands(script, 0) == std::vector<Edit>{{Op::Delete, 0, 1}, {Op::Insert, 0, 0}, {Op::Keep, 1, 1}});
}
