// tests/beman/specgen/json/reader.test.cpp                        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/json/reader.hpp>
#include <beman/specgen/json/reader.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using beman::specgen::json::Reader;

// The lexer on its own terms. descriptor.test.cpp exercises it through the
// schema walkers; these cases pin the primitives and where a failure lands.

TEST_CASE("json_reader - parse_string reads the named escapes") {
    Reader      r(R"("a\"b\\c\/d\b\f\n\r\t")");
    std::string out;
    REQUIRE(r.parse_string(out));
    CHECK(out == "a\"b\\c/d\b\f\n\r\t");
}

TEST_CASE("json_reader - parse_string decodes a \\u escape as UTF-8") {
    Reader      r(R"("\u001fé€")");
    std::string out;
    REQUIRE(r.parse_string(out));
    CHECK(out == "\x1f\xc3\xa9\xe2\x82\xac");
}

TEST_CASE("json_reader - parse_string fails on an unterminated string or a bad escape") {
    std::string out;
    Reader      unterminated(R"("abc)");
    CHECK_FALSE(unterminated.parse_string(out));
    CHECK(unterminated.message() == "unterminated string");

    Reader unknown(R"("a\qb")");
    CHECK_FALSE(unknown.parse_string(out));
    CHECK(unknown.message() == "unknown escape");

    Reader bad_hex(R"("\u00zz")");
    CHECK_FALSE(bad_hex.parse_string(out));
    CHECK(bad_hex.message() == "bad hex digit in \\u escape");
}

TEST_CASE("json_reader - parse_uint and parse_bool skip leading whitespace") {
    Reader      r(" 42 \n true\tfalse");
    std::size_t n = 0;
    bool        t = false;
    bool        f = true;
    REQUIRE(r.parse_uint(n));
    REQUIRE(r.parse_bool(t));
    REQUIRE(r.parse_bool(f));
    CHECK(n == 42);
    CHECK(t);
    CHECK_FALSE(f);
    CHECK(r.at_end());
}

TEST_CASE("json_reader - object and array hand each member and element to the caller") {
    Reader                   r(R"({"a": [1, 2], "b": []})");
    std::vector<std::string> keys;
    std::vector<std::size_t> values;
    const bool               ok = r.object([&](std::string_view key) {
        keys.emplace_back(key);
        return r.array([&] {
            std::size_t v    = 0;
            const bool  read = r.parse_uint(v);
            values.push_back(v);
            return read;
        });
    });
    REQUIRE(ok);
    CHECK(keys == std::vector<std::string>{"a", "b"});
    CHECK(values == std::vector<std::size_t>{1, 2});
}

TEST_CASE("json_reader - skip_value discards a value of any shape") {
    Reader r(R"({"x": [true, false, null, "s", {"y": 3}]} 7)");
    REQUIRE(r.skip_value());
    std::size_t after = 0;
    REQUIRE(r.parse_uint(after));
    CHECK(after == 7);
}

TEST_CASE("json_reader - the first failure is the one recorded") {
    Reader r(R"({"a" 1})");
    CHECK_FALSE(r.object([](std::string_view) { return true; }));
    CHECK(r.failed());
    CHECK(r.offset() == 5);
    const std::string first = r.message();
    r.fail("a later failure");
    CHECK(r.message() == first);
}
