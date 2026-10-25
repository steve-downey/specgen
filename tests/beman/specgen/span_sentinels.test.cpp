// tests/beman/specgen/span_sentinels.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/span_sentinels.hpp> // Re-inclusion verification

#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

namespace ir = beman::specgen::ir;
using namespace beman::specgen::span_sentinels;

TEST_CASE("span_sentinels - a sentinel is a distinct identifier per index") {
    CHECK(span_sentinel(0) == "SPECGEN_0_SPAN");
    CHECK(span_sentinel(0) != span_sentinel(1));
}

TEST_CASE("span_sentinels - recovery without sentinels leaves the text alone") {
    const ir::CodeText code = recover_sentinels("int f();", {});
    CHECK(code.text == "int f();");
    CHECK(code.spans.empty());
}

TEST_CASE("span_sentinels - recovery swaps each sentinel for its display text and records a span") {
    const std::map<std::string, SpanInfo> sentinels = {
        {span_sentinel(0), SpanInfo{ir::SpanKind::ExposId, "val", "val"}},
        {span_sentinel(1), SpanInfo{ir::SpanKind::SeeBelow, "see below", ""}},
    };
    const ir::CodeText code = recover_sentinels(span_sentinel(1) + " f(" + span_sentinel(0) + ");", sentinels);
    CHECK(code.text == "see below f(val);");
    REQUIRE(code.spans.size() == 2);
    // In text order, whatever order the table holds them in.
    CHECK(code.spans[0].kind == ir::SpanKind::SeeBelow);
    CHECK(code.text.substr(code.spans[0].begin, code.spans[0].end - code.spans[0].begin) == "see below");
    CHECK(code.spans[1].kind == ir::SpanKind::ExposId);
    CHECK(code.spans[1].payload == "val");
    CHECK(code.text.substr(code.spans[1].begin, code.spans[1].end - code.spans[1].begin) == "val");
}

TEST_CASE("span_sentinels - a sentinel absent from the text records no span") {
    const std::map<std::string, SpanInfo> sentinels = {
        {span_sentinel(0), SpanInfo{ir::SpanKind::ExposId, "val", "val"}},
    };
    const ir::CodeText code = recover_sentinels("int f();", sentinels);
    CHECK(code.text == "int f();");
    CHECK(code.spans.empty());
}

TEST_CASE("span_sentinels - resizing makes each sentinel exactly as wide as its display text") {
    const std::map<std::string, SpanInfo> sentinels = {
        {span_sentinel(0), SpanInfo{ir::SpanKind::ExposId, "value-type", "value-type"}},
    };
    const std::string text             = "using T = " + span_sentinel(0) + ";";
    const auto [resized_text, resized] = resize_sentinels(text, sentinels);
    REQUIRE(resized.size() == 1);
    const std::string& name = resized.begin()->first;
    CHECK(name.size() == std::string("value-type").size());
    CHECK(resized_text == "using T = " + name + ";");
    // The round trip lands on the same text the unresized sentinel would.
    CHECK(recover_sentinels(resized_text, resized).text == recover_sentinels(text, sentinels).text);
}

TEST_CASE("span_sentinels - a sentinel absent from the text keeps its name through resizing") {
    const std::map<std::string, SpanInfo> sentinels = {
        {span_sentinel(0), SpanInfo{ir::SpanKind::ExposId, "val", "val"}},
    };
    const auto [resized_text, resized] = resize_sentinels("int f();", sentinels);
    CHECK(resized_text == "int f();");
    CHECK(resized.contains(span_sentinel(0)));
}
