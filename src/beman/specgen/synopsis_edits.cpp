// src/beman/specgen/synopsis_edits.cpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/synopsis_edits.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace beman::specgen::synopsis_edits {

void sort_back_to_front(std::vector<SynopsisEdit>& edits) {
    std::ranges::stable_sort(edits, [](const SynopsisEdit& a, const SynopsisEdit& b) {
        return a.begin > b.begin || (a.begin == b.begin && a.end > b.end);
    });
}

bool edits_overlap(const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin < b.end && b.begin < a.end; }

void add_dominant_edit(std::vector<SynopsisEdit>& edits, SynopsisEdit dominant) {
    std::erase_if(edits, [&](const SynopsisEdit& edit) { return edits_overlap(edit, dominant); });
    edits.push_back(std::move(dominant));
}

} // namespace beman::specgen::synopsis_edits
