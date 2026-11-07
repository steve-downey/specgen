// tests/beman/specgen/resume_body.test.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/lower.hpp>

#include <catch2/catch_test_macros.hpp>

#include <variant>

namespace grammar = beman::specgen::grammar;
namespace ir      = beman::specgen::ir;

TEST_CASE("description lists resume prose without opening another numbered paragraph") {
    const auto parsed = grammar::parse_docblock(R"(//! \ensures A result:
//! \item One, with
//! a wrapped continuation.
//!
//! \item Two.
//!
//! Otherwise, unchanged.
//!
//! A separate paragraph.
//! \item Three.
//!
//! A final continuation.
//! \remarks A different element.
)");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.diags.empty());
    const auto lowered = beman::specgen::lowering::lower(parsed.block);
    REQUIRE(lowered.descr.elements.size() == 2);
    const auto& body = lowered.descr.elements.front().body;
    REQUIRE(body.size() == 6);
    REQUIRE(std::holds_alternative<ir::BodyParagraph>(body[0]));
    REQUIRE(std::holds_alternative<ir::Itemize>(body[1]));
    CHECK(std::get<ir::Itemize>(body[1]).items.size() == 2);
    REQUIRE(std::holds_alternative<ir::BodyParagraph>(body[2]));
    CHECK(std::get<ir::BodyParagraph>(body[2]).continuation);
    CHECK_FALSE(std::get<ir::BodyParagraph>(body[3]).continuation);
    REQUIRE(std::holds_alternative<ir::Itemize>(body[4]));
    CHECK(std::get<ir::Itemize>(body[4]).items.size() == 1);
    CHECK(std::get<ir::BodyParagraph>(body[5]).continuation);
}
