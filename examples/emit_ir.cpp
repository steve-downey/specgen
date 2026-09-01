// examples/emit_ir.cpp                                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Minimal end-to-end use of the clang-free core: parse a docblock markup
// block, hand-build a matching IR item, and emit the item as `--emit-ir` JSON.

#include <beman/specgen/docblock.hpp>
#include <beman/specgen/ir.hpp>

#include <print>

int main() {
    namespace grammar = beman::specgen::grammar;
    namespace ir      = beman::specgen::ir;

    // 1. Parse a sparse markup block (as the front end will hand it to us).
    const auto parsed = grammar::parse_docblock("//! \\mandates `is_copy_constructible_v<T>` is `true`.\n"
                                                "//! \\effects-equiv\n");
    for (const auto& d : parsed.diags)
        std::println(stderr, "docblock:{}: {}", d.line, d.message);
    if (!parsed.ok())
        return 1;

    // 2. Hand-build the corresponding IR item (the front end derives this from the AST).
    ir::SpecItem item;
    item.decl.signatures.push_back(
        {"template <class U = remove_cv_t<T>>\nconstexpr remove_cv_t<T> value_or(U&& u) const&;", {}});
    item.decl.index.push_back({ir::IndexKind::Member, "value_or", "optional"});

    ir::DescriptionElement mandates;
    mandates.kind = ir::ElementKind::Mandates;
    mandates.paragraphs.push_back({ir::CodeInline{{"is_copy_constructible_v<T>", {}}},
                                   ir::TextInline{" is "},
                                   ir::CodeInline{{"true", {}}},
                                   ir::TextInline{"."}});
    item.descr.elements.push_back(std::move(mandates));

    ir::DescriptionElement effects;
    effects.kind = ir::ElementKind::Effects;
    effects.equivalent =
        ir::EquivalentTo{{"return has_value() ? *(*this) : static_cast<remove_cv_t<T>>(std::forward<U>(u));", {}}};
    item.descr.elements.push_back(std::move(effects));

    // 3. Canonical [structure.specifications] order, then serialize.
    ir::canonicalize(item.descr);
    std::println(stdout, "{}", ir::emit_json(item));
    return 0;
}
