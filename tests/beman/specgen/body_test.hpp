// tests/beman/specgen/body_test.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_TEST_BODY_TEST_HPP
#define BEMAN_SPECGEN_TEST_BODY_TEST_HPP

#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::test {

template <typename T>
const T* first(const ir::DescriptionElement& e) {
    const auto it = std::ranges::find_if(e.body, [](const ir::BodyBlock& b) { return std::holds_alternative<T>(b); });
    return it == e.body.end() ? nullptr : &std::get<T>(*it);
}

template <typename T>
T* first(ir::DescriptionElement& e) {
    const auto it = std::ranges::find_if(e.body, [](const ir::BodyBlock& b) { return std::holds_alternative<T>(b); });
    return it == e.body.end() ? nullptr : &std::get<T>(*it);
}

inline std::size_t paragraph_count(const ir::DescriptionElement& e) {
    return static_cast<std::size_t>(std::ranges::count_if(
        e.body, [](const ir::BodyBlock& b) { return std::holds_alternative<ir::BodyParagraph>(b); }));
}

inline const ir::Paragraph& paragraph(const ir::DescriptionElement& e, std::size_t index = 0) {
    for (const auto& b : e.body) {
        if (const auto* p = std::get_if<ir::BodyParagraph>(&b)) {
            if (index == 0)
                return p->text;
            --index;
        }
    }
    throw std::out_of_range("description paragraph");
}

inline ir::DescriptionBody prose(std::vector<ir::Paragraph> paragraphs) {
    ir::DescriptionBody body;
    for (auto& p : paragraphs)
        body.push_back(ir::BodyParagraph{std::move(p)});
    return body;
}

inline ir::DescriptionElement
element(ir::ElementKind kind, std::vector<ir::Paragraph> paragraphs = {}, std::optional<ir::Itemize> list = {}) {
    ir::DescriptionElement e;
    e.kind = kind;
    e.body = prose(std::move(paragraphs));
    if (list)
        e.body.push_back(std::move(*list));
    return e;
}

} // namespace beman::specgen::test
namespace body_test = beman::specgen::test;

#endif // BEMAN_SPECGEN_TEST_BODY_TEST_HPP
