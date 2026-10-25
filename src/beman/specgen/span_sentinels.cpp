// src/beman/specgen/span_sentinels.cpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/span_sentinels.hpp>

#include <beman/specgen/ir.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::span_sentinels {

namespace {

// An identifier of exactly `width` characters that occurs neither in `text` nor
// among `taken`, or nullopt when none can be built (a width of zero, or every
// candidate already present -- neither has happened, but a caller that gets
// nullopt keeps the sentinel it has, which is what the code did before this).
//
// The letters are arbitrary; what matters is that a run of one letter followed
// by a counter is not something C++ source contains, and that the result is
// checked rather than assumed.
std::optional<std::string>
exact_width_sentinel(const std::string& text, const std::set<std::string>& taken, std::size_t width, unsigned n) {
    if (width == 0)
        return std::nullopt;
    // substrate generic algorithm: try letters until one yields a candidate
    // absent from the text -- a search whose subject is the text, not a range
    // that already exists.
    for (const char filler : std::string_view("ZQXJVWKY")) {
        std::string candidate = std::format("{}{}", filler, n);
        if (candidate.size() > width)
            candidate.resize(width);
        candidate.resize(width, filler);
        if (!taken.contains(candidate) && text.find(candidate) == std::string::npos)
            return candidate;
    }
    return std::nullopt;
}

} // namespace

beman::specgen::ir::CodeText recover_sentinels(std::string text, const std::map<std::string, SpanInfo>& sentinels) {
    namespace ir = beman::specgen::ir;

    if (sentinels.empty())
        return ir::CodeText{std::move(text), {}};

    // Each sentinel is unique and occurs once; collect the hits and order them.
    struct Hit {
        std::size_t        pos;
        const std::string* name;
        const SpanInfo*    info;
    };
    std::vector<Hit> hits =
        sentinels | std::views::filter([&](const auto& kv) { return text.find(kv.first) != std::string::npos; }) |
        std::views::transform([&](const auto& kv) {
            const auto& [name, info] = kv;
            return Hit{text.find(name), &name, &info};
        }) |
        std::ranges::to<std::vector<Hit>>();
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.pos < b.pos; });

    ir::CodeText out;
    std::size_t  src = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // over a sorted, non-overlapping hit table -- the same array-substitution
    // primitive backend/code_spans.hpp's render_code_spans already marks -- not a
    // fold in disguise, since a hit's recorded span begin depends on how much
    // the walk has already appended, which std::ranges::fold_left would have
    // to smuggle through a synthetic multi-field accumulator for no gain in
    // clarity over the loop.
    for (const Hit& hit : hits) {
        out.text += text.substr(src, hit.pos - src);
        const std::size_t begin = out.text.size();
        out.text += hit.info->display;
        out.spans.push_back(ir::Span{begin, out.text.size(), hit.info->kind, hit.info->payload});
        src = hit.pos + hit.name->size();
    }
    out.text += text.substr(src);
    return out;
}

std::pair<std::string, std::map<std::string, SpanInfo>>
resize_sentinels(std::string text, const std::map<std::string, SpanInfo>& sentinels) {
    std::map<std::string, SpanInfo> resized;
    std::set<std::string>           taken;
    unsigned                        n = 0;
    // substrate generic algorithm: a scatter into two coupled outputs -- the
    // rewritten text and the re-keyed table -- where each step's replacement
    // depends on what the previous ones claimed.
    for (const auto& [sentinel, info] : sentinels) {
        const std::size_t                at = text.find(sentinel);
        const std::optional<std::string> fitted =
            at == std::string::npos ? std::nullopt : exact_width_sentinel(text, taken, info.display.size(), n++);
        if (!fitted) {
            resized.emplace(sentinel, info);
            taken.insert(sentinel);
            continue;
        }
        text.replace(at, sentinel.size(), *fitted);
        resized.emplace(*fitted, info);
        taken.insert(*fitted);
    }
    return {std::move(text), std::move(resized)};
}

} // namespace beman::specgen::span_sentinels
