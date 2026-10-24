// src/beman/specgen/markers.cpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/markers.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace beman::specgen::grammar {

namespace detail {

bool set_expos_name(Markers& markers, std::string arg) {
    const bool had     = markers.expos_name.has_value();
    markers.expos_name = std::move(arg);
    return had;
}

bool set_at_anchor(Markers& markers, std::string arg) {
    const bool had    = markers.at_anchor.has_value();
    markers.at_anchor = std::move(arg);
    return had;
}

bool set_seebelow_target(Markers& markers, std::string arg) {
    const bool had          = markers.seebelow_target.has_value();
    markers.seebelow_target = std::move(arg);
    return had;
}

bool set_group_id(Markers& markers, std::string arg) {
    const bool had   = markers.group_id.has_value();
    markers.group_id = std::move(arg);
    return had;
}

bool set_also_target(Markers& markers, std::string arg) {
    const bool had      = markers.also_target.has_value();
    markers.also_target = std::move(arg);
    return had;
}

} // namespace detail

const MarkerInfo* find_marker(std::string_view name) {
    const auto it = std::ranges::find_if(kMarkers, [name](const MarkerInfo& info) { return info.spelling == name; });
    return it != std::ranges::end(kMarkers) ? &*it : nullptr;
}

} // namespace beman::specgen::grammar
