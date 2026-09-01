// tests/beman/specgen/foundation/json_writer.test.cpp           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/foundation/json_writer.hpp>
#include <beman/specgen/foundation/json_writer.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <string>

using beman::specgen::foundation::json_array;
using beman::specgen::foundation::json_object;
using beman::specgen::foundation::write_json_string;

TEST_CASE("json_writer - write_json_string quotes a plain string") {
    std::string out;
    write_json_string("hello", out);
    CHECK(out == R"("hello")");
}

TEST_CASE("json_writer - write_json_string escapes the named escapes") {
    std::string out;
    write_json_string("a\"b\\c\nd\te\rf", out);
    CHECK(out == R"("a\"b\\c\nd\te\rf")");
}

TEST_CASE("json_writer - write_json_string escapes an unnamed control character as lowercase \\u00XX") {
    // Control-character-to-\u00XX escaping (the quirk the round-trip goldens
    // depend on) is exercised end-to-end by ir.test.cpp's
    // "round-trip: strings needing escapes"; this covers write_json_string in
    // isolation for one representative byte.
    const std::string control(1, static_cast<char>(0x01));
    std::string       out;
    write_json_string(control, out);
    CHECK(out == "\"\\u0001\"");
}

TEST_CASE("json_writer - write_json_string escapes a control character above 0x0f with lowercase hex") {
    // 0x1f is the byte that catches a `{:02X}` typo in write_json_string's
    // format string: 0x01 renders the
    // same in either case, so the case above cannot see the difference.
    const std::string control(1, static_cast<char>(0x1f));
    std::string       out;
    write_json_string(control, out);
    CHECK(out == "\"\\u001f\"");
}

TEST_CASE("json_writer - write_json_string leaves an empty string as two quotes") {
    std::string out;
    write_json_string("", out);
    CHECK(out == R"("")");
}

TEST_CASE("json_writer - write_json_string appends rather than replacing") {
    // The sink is a string the caller may already have written into -- every
    // nested scope in the emitter relies on that.
    std::string out = "prefix:";
    write_json_string("x", out);
    CHECK(out == R"(prefix:"x")");
}

TEST_CASE("json_writer - json_array writes empty brackets for no elements") {
    std::string out;
    {
        json_array arr(out);
    }
    CHECK(out == "[]");
}

TEST_CASE("json_writer - json_array separates elements with commas, none trailing") {
    std::string out;
    {
        json_array arr(out);
        arr.element() += "1";
        arr.element() += "2";
        arr.element() += "3";
    }
    CHECK(out == "[1,2,3]");
}

TEST_CASE("json_writer - json_object writes empty braces for no members") {
    std::string out;
    {
        json_object obj(out);
    }
    CHECK(out == "{}");
}

TEST_CASE("json_writer - json_object separates members with commas, none trailing") {
    std::string out;
    {
        json_object obj(out);
        obj.key("a") += "1";
        obj.key("b") += "2";
    }
    CHECK(out == R"({"a":1,"b":2})");
}

TEST_CASE("json_writer - json_object key names are themselves escaped") {
    std::string out;
    {
        json_object obj(out);
        obj.key("has\"quote") += "1";
    }
    CHECK(out == R"({"has\"quote":1})");
}

TEST_CASE("json_writer - scopes nest by C++ lifetime: an array of objects") {
    std::string out;
    {
        json_array outer(out);
        {
            json_object item(outer.element());
            write_json_string("x", item.key("name"));
        }
        {
            json_object item(outer.element());
            write_json_string("y", item.key("name"));
        }
    }
    CHECK(out == R"([{"name":"x"},{"name":"y"}])");
}

TEST_CASE("json_writer - scopes nest by C++ lifetime: an object with an array member") {
    std::string out;
    {
        json_object obj(out);
        write_json_string("widget", obj.key("kind"));
        {
            json_array children(obj.key("children"));
            children.element() += "1";
            children.element() += "2";
        }
    }
    CHECK(out == R"({"kind":"widget","children":[1,2]})");
}
