// tests/beman/specgen/foundation/json_descriptor.test.cpp        -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/foundation/json_descriptor.hpp>
#include <beman/specgen/foundation/json_descriptor.hpp> // Re-inclusion verification

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <tuple>

using namespace beman::specgen::foundation;

TEST_CASE("json_descriptor - HeaderIsIdempotent") { REQUIRE(true); }

namespace {

// A tiny type local to this test, independent of ir.hpp, so the vocabulary
// is exercised on its own terms rather than through ir.cpp's real schema.
struct Widget {
    std::string name;
    std::size_t count = 0;
};

struct OptionalWidget {
    std::string                name;
    std::optional<std::string> note;
};

} // namespace

TEST_CASE("json_descriptor - an explicit member table round-trips a plain object") {
    constexpr auto members = std::tuple{field("name", &Widget::name), field("count", &Widget::count)};

    Widget      w{"widget", 3};
    std::string json;
    emit_json_described(w, json, members);
    CHECK(json == R"({"name":"widget","count":3})");

    Reader r(json);
    Widget out;
    REQUIRE(parse_json_described(r, out, members));
    CHECK(out.name == "widget");
    CHECK(out.count == 3);
}

TEST_CASE("json_descriptor - unknown keys are skipped, matching Reader::skip_value") {
    constexpr auto members = std::tuple{field("name", &Widget::name)};
    Reader         r(R"({"note":"ignored","name":"widget","extra":[1,2,{"x":true}]})");
    Widget         out;
    REQUIRE(parse_json_described(r, out, members));
    CHECK(out.name == "widget");
}

TEST_CASE("json_descriptor - optional fields retain their value shape and omit absence") {
    constexpr auto members =
        std::tuple{field("name", &OptionalWidget::name), optional_field("note", &OptionalWidget::note)};

    std::string json;
    emit_json_described(OptionalWidget{"widget", "present"}, json, members);
    CHECK(json == R"({"name":"widget","note":"present"})");

    Reader         present_reader(json);
    OptionalWidget present;
    REQUIRE(parse_json_described(present_reader, present, members));
    REQUIRE(present.note.has_value());
    CHECK(*present.note == "present");

    json.clear();
    emit_json_described(OptionalWidget{"widget", std::nullopt}, json, members);
    CHECK(json == R"({"name":"widget"})");
    Reader         absent_reader(json);
    OptionalWidget absent;
    REQUIRE(parse_json_described(absent_reader, absent, members));
    CHECK_FALSE(absent.note.has_value());
}

// --- the meta-test decision json-single-schema owes -------------------------
//
// Two hand-written descriptions of the same schema -- an emit half and a
// parse half -- are free to drift: nothing forces
// a field renamed on one side to be renamed on the other. The whole point of
// the shared table is that emit and parse read the *same* `constexpr` table,
// so that drift is not expressible for a real IR type -- there is only one
// table to consult, not two to keep in sync.
//
// This test demonstrates the hazard directly, on the Widget type above,
// rather than merely asserting on ir.cpp's already-passing round-trip tests
// (which would keep passing whether or not the tables were truly shared).
// The two tables below reproduce the two-table shape -- "name" spelled one
// way for emission, another for parsing -- entirely within this test, using
// the same emit_json_described/parse_json_described primitives ir.cpp's
// json_descriptor<T> specializations feed. When a type instead has exactly
// one specialization (as every ir.cpp type does), this failure mode is
// structurally unreachable: there is nowhere for a second, differently
// spelled table to come from.
TEST_CASE("json_descriptor - two tables for one type is exactly the hazard the shared table forecloses") {
    constexpr auto emit_side  = std::tuple{field("name", &Widget::name), field("count", &Widget::count)};
    constexpr auto parse_side = std::tuple{field("nom", &Widget::name), field("count", &Widget::count)};

    Widget      w{"widget", 3};
    std::string json;
    emit_json_described(w, json, emit_side);

    Reader r(json);
    Widget out;
    // The object still parses -- unknown keys are skipped -- but the
    // mis-matched field name means the value is silently lost rather than
    // rejected. A single shared table (one `field("name", ...)`, referenced
    // by both directions) is what makes this outcome impossible for the
    // real ir.cpp types: mutate that one table, and both emit and parse see
    // the mutation identically, by construction.
    REQUIRE(parse_json_described(r, out, parse_side));
    CHECK(out.name.empty());
    CHECK(out.count == 3);
}

namespace {

// A minimal tagged-variant fixture, exercising alt/alternatives the same way
// ir.cpp's Node and Inline descriptors do.
struct Circle {
    std::size_t radius = 0;
};
struct Square {
    std::size_t side = 0;
};
using Shape = std::variant<Circle, Square>;

} // namespace

template <>
struct beman::specgen::foundation::json_descriptor<Circle> {
    static constexpr auto members = std::tuple{field("radius", &Circle::radius)};
};
template <>
struct beman::specgen::foundation::json_descriptor<Square> {
    static constexpr auto members = std::tuple{field("side", &Square::side)};
};
template <>
struct beman::specgen::foundation::json_descriptor<Shape> {
    static constexpr auto alts = alternatives("kind", "shape kind", alt<Circle>("circle"), alt<Square>("square"));
};

TEST_CASE("json_descriptor - a tagged variant round-trips, tag key order-independent") {
    const Shape shape = Square{5};
    std::string json;
    emit_variant(shape, json);
    CHECK(json == R"({"kind":"square","side":5})");

    Reader r(json);
    Shape  out;
    REQUIRE(parse_value(r, out));
    REQUIRE(std::holds_alternative<Square>(out));
    CHECK(std::get<Square>(out).side == 5);

    // The tag may come after the fields it selects among.
    Reader r2(R"({"radius":7,"kind":"circle"})");
    Shape  out2;
    REQUIRE(parse_value(r2, out2));
    REQUIRE(std::holds_alternative<Circle>(out2));
    CHECK(std::get<Circle>(out2).radius == 7);
}

TEST_CASE("json_descriptor - a tagged variant rejects an unknown tag") {
    Reader r(R"({"kind":"triangle"})");
    Shape  out;
    CHECK(!parse_value(r, out));
    CHECK(r.message().find("triangle") != std::string::npos);
}

TEST_CASE("json_descriptor - vector<T> emits and parses as a JSON array") {
    std::string out_json;
    emit_value(std::vector<std::size_t>{1, 2, 3}, out_json);
    CHECK(out_json == "[1,2,3]");

    Reader                   r("[4,5]");
    std::vector<std::size_t> out;
    REQUIRE(parse_value(r, out));
    CHECK(out == std::vector<std::size_t>{4, 5});
}
