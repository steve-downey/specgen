// tests/beman/specgen/frontend/base_members.test.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/base_members.hpp>
#include <beman/specgen/frontend/base_members.hpp> // Re-inclusion verification

#include <beman/specgen/frontend/build_document.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/validate/validate.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace frontend = beman::specgen::frontend;
namespace ir       = beman::specgen::ir;
namespace validate = beman::specgen::validate;

namespace {

const ir::Synopsis& wrapper_synopsis(const ir::Document& document) {
    const auto& section = std::get<ir::Section>(document.nodes.back());
    return std::get<ir::Synopsis>(*std::ranges::find_if(section.children, [](const ir::Node& node) {
        const auto* synopsis = std::get_if<ir::Synopsis>(&node);
        return synopsis != nullptr && synopsis->name == "wrapper";
    }));
}

} // namespace

TEST_CASE("base_members - each class a member may come from, a dependent base giving each specialization") {
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_base_members.hpp");
    REQUIRE(built.has_value());
    const ir::Synopsis& synopsis = wrapper_synopsis(built->document);
    REQUIRE(synopsis.declared.has_value());

    // The primary call_base is not defined, so its two partial
    // specializations and `observers`.
    const std::vector<ir::CodeText>& bases = synopsis.declared_bases;
    REQUIRE(bases.size() == 3);
    const auto declares = [&](std::string_view text) {
        return std::ranges::count_if(bases, [&](const ir::CodeText& base) { return base.text.contains(text); });
    };
    CHECK(declares("constexpr R operator()(Args ...args);") == 1);
    CHECK(declares("constexpr R operator()(Args ...args) const;") == 1);
    CHECK(declares("constexpr bool empty() const noexcept;") == 1);
    // A private member, and an assignment a derived class declares itself, are not inherited.
    CHECK(declares("private_helper") == 0);
    CHECK(declares("operator=") == 0);
}

TEST_CASE("base_members - a synopsis line is checked against the base that declares it") {
    const auto built = frontend::build_document(std::string(BEMAN_SPECGEN_CORPUS_DIR) + "/spec_base_members.hpp");
    REQUIRE(built.has_value());
    std::vector<std::string> messages;
    std::ranges::for_each(validate::validate(built->document), [&](const validate::Diagnostic& d) {
        if (d.context.ends_with("wrapper/synopsis"))
            messages.push_back(validate::format_diagnostic(d));
    });
    REQUIRE(messages.size() == 2);
    CHECK(
        messages[0].ends_with("warning: `unsigned size() const noexcept`: the synopsis does not have `constexpr` and "
                              "the base class's declaration does"));
    CHECK(messages[1].ends_with("note: `void reset()`: the synopsis declares this, and the code does not"));
}
