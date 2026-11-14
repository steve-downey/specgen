// tests/beman/specgen/synopsis_check.test.cpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/synopsis_check.hpp>
#include <beman/specgen/synopsis_check.hpp> // Re-inclusion verification

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/ir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

using namespace beman::specgen::ir;
namespace check = beman::specgen::synopsis_check;

namespace {

CodeText code(std::string text) { return {std::move(text), {}}; }

constexpr const char* kDraft = R"(namespace std {
  template<class R, class... ArgTypes>
  class function {
  public:
    // \ref{ctor}, constructors
    function() noexcept;
    template<class F> function(F&&);
    template<class T, class... Args>
      explicit function(in_place_type_t<T>, Args&&...);

    function& operator=(function&&);
    explicit operator bool() const noexcept;
    R operator()(ArgTypes... args) const;
    void swap(function& other) noexcept;
    friend void swap(function&, function&) noexcept;
  };
})";

constexpr const char* kConstexpr = R"(namespace std {
  template<class R, class... ArgTypes>
  class function {
  public:
    // \ref{ctor}, constructors
    constexpr function() noexcept;
    template<class F> constexpr function(F&&);
    template<class T, class... Args>
      constexpr explicit function(in_place_type_t<T>, Args&&...);

    constexpr function& operator=(function&&);
    constexpr explicit operator bool() const noexcept;
    constexpr R operator()(ArgTypes... args) const;
    constexpr void swap(function& other) noexcept;
    friend constexpr void swap(function&, function&) noexcept;
  };
})";

} // namespace

TEST_CASE("synopsis_check - a synopsis that agrees with its declarations has no findings") {
    CHECK(check::compare(code(kDraft), code(kDraft)).empty());
    CHECK(check::compare(code(kConstexpr), code(kConstexpr)).empty());
}

TEST_CASE("synopsis_check - a specifier the declarations add is a finding for each declaration") {
    const auto findings = check::compare(code(kDraft), code(kConstexpr));
    REQUIRE(findings.size() == 8);
    for (const auto& finding : findings)
        CHECK(finding.message == "the synopsis does not have `constexpr` and the declaration does");
}

TEST_CASE("synopsis_check - patching writes the declarations' specifiers into the draft's synopsis") {
    const auto patched = check::patch(code(kDraft), code(kConstexpr));
    CHECK(patched.unpatched.empty());
    // Only `constexpr` was added: the draft's layout and comment are the draft's.
    CHECK(patched.code.text == kConstexpr);
}

TEST_CASE("synopsis_check - noexcept and explicit are patched both ways") {
    const auto draft = code("class f {\n  explicit f(int);\n  void g();\n  void h() noexcept;\n};");
    const auto code2 = code("class f {\n  f(int);\n  void g() noexcept;\n  void h();\n};");

    const auto patched = check::patch(draft, code2);
    CHECK(patched.unpatched.empty());
    CHECK(patched.code.text == code2.text);
    CHECK(check::compare(patched.code, code2).empty());
}

TEST_CASE("synopsis_check - the declarations' noexcept operand is written as they have it") {
    const auto draft    = code("class f {\n  void g();\n};");
    const auto declared = code("class f {\n  void g() noexcept(is_nothrow_v<T>);\n};");
    CHECK(check::patch(draft, declared).code.text == declared.text);
}

TEST_CASE("synopsis_check - spans move with the text they sit on") {
    CodeText draft;
    draft.text          = "class f {\n  void g();\n  void h(cv);\n};";
    draft.spans         = {{draft.text.find("cv"), draft.text.find("cv") + 2, SpanKind::Placeholder, "cv"}};
    const auto declared = code("class f {\n  void g();\n  constexpr void h(cv);\n};");

    const auto patched = check::patch(draft, declared);
    REQUIRE(patched.code.spans.size() == 1);
    const Span& span = patched.code.spans.front();
    CHECK(patched.code.text.substr(span.begin, span.end - span.begin) == "cv");
    CHECK(patched.code.text.contains("constexpr void h(cv);"));
}

TEST_CASE("synopsis_check - a specifier that cannot be patched is reported, and a lone declaration is a finding") {
    const auto draft    = code("class f {\n  void g();\n  void only_here();\n};");
    const auto declared = code("class f {\n  static void g();\n  void only_there();\n};");

    const auto patched = check::patch(draft, declared);
    CHECK(patched.code.text == draft.text);
    REQUIRE(patched.unpatched.size() == 3);

    const auto findings = check::compare(draft, declared);
    REQUIRE(findings.size() == 3);
    CHECK(findings[0].message == "the synopsis does not have `static` and the declaration does");
    CHECK(findings[1].message == "the synopsis declares this, and the code does not");
    CHECK(findings[1].severity == beman::specgen::Severity::Note);
    CHECK(findings[0].severity == beman::specgen::Severity::Warning);
    CHECK(findings[2].message == "the code declares this, and the synopsis does not");
}

TEST_CASE("synopsis_check - overloads pair with the declaration that reads most like them") {
    const auto draft    = code("class f {\n  f(int);\n  f(const string&);\n};");
    const auto declared = code("class f {\n  constexpr f(const string&);\n  f(int);\n};");

    const auto patched = check::patch(draft, declared);
    CHECK(patched.code.text == "class f {\n  f(int);\n  constexpr f(const string&);\n};");
}

TEST_CASE("synopsis_check - aliases, friend classes and access labels are not declarations") {
    const auto text = code(
        "class f {\npublic:\n  using t = int;\n  friend class g;\n  static_assert(true);\nprivate:\n  void h();\n};");
    CHECK(check::compare(text, text).empty());
    const auto without  = code("class f {\n};");
    const auto findings = check::compare(text, without);
    REQUIRE(findings.size() == 1);
    CHECK(findings.front().declaration == "void h()");
}

TEST_CASE("synopsis_check - a placeholder in the noexcept operand agrees with either") {
    CodeText draft;
    draft.text       = "class f {\n  R g() cv noexcept(noex);\n};";
    draft.spans      = {{draft.text.find("noex)"), draft.text.find("noex)") + 4, SpanKind::Placeholder, "noex"}};
    const auto plain = code("class f {\n  R g();\n};");

    CHECK(check::compare(draft, plain).empty());
    CHECK(check::patch(draft, plain).code.text == draft.text);
}
