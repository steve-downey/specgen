// src/beman/specgen/ir.cpp                                         -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/ir.hpp>

#include <beman/specgen/foundation/enumeration.hpp>

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace beman::specgen::ir {

namespace {

using namespace std::string_view_literals;

// One total table per enumeration: an enumerator missing from it fails to
// compile (foundation/enumeration.hpp), where a hand-sized array or a switch
// with a fallback would answer "?" in its place. The names are part of the
// JSON schema a hand-written validate-mode golden spells out, so the reverse
// lookup reads the same table, and one table cannot drift from itself.
inline constexpr auto kElementNames =
    foundation::table<ElementKind>(foundation::is<ElementKind::Constraints>("constraints"sv),
                                   foundation::is<ElementKind::Mandates>("mandates"sv),
                                   foundation::is<ElementKind::Expects>("expects"sv),
                                   foundation::is<ElementKind::HardExpects>("hardexpects"sv),
                                   foundation::is<ElementKind::Effects>("effects"sv),
                                   foundation::is<ElementKind::Sync>("sync"sv),
                                   foundation::is<ElementKind::Ensures>("ensures"sv),
                                   foundation::is<ElementKind::Result>("result"sv),
                                   foundation::is<ElementKind::Returns>("returns"sv),
                                   foundation::is<ElementKind::Throws>("throws"sv),
                                   foundation::is<ElementKind::Complexity>("complexity"sv),
                                   foundation::is<ElementKind::Remarks>("remarks"sv),
                                   foundation::is<ElementKind::Errors>("errors"sv));

inline constexpr auto kDispositionNames =
    foundation::table<Disposition>(foundation::is<Disposition::Described>("described"sv),
                                   foundation::is<Disposition::Routed>("routed"sv),
                                   foundation::is<Disposition::Merged>("merged"sv),
                                   foundation::is<Disposition::Omitted>("omitted"sv),
                                   foundation::is<Disposition::Defaulted>("defaulted"sv),
                                   foundation::is<Disposition::Declared>("declared"sv),
                                   foundation::is<Disposition::Expos>("expos"sv),
                                   foundation::is<Disposition::Private>("private"sv),
                                   foundation::is<Disposition::Undocumented>("undocumented"sv));

inline constexpr auto kMemberKindNames =
    foundation::table<MemberKind>(foundation::is<MemberKind::Function>("function"sv),
                                  foundation::is<MemberKind::Data>("data"sv),
                                  foundation::is<MemberKind::Alias>("alias"sv));

inline constexpr auto kSpanKindNames =
    foundation::table<SpanKind>(foundation::is<SpanKind::ExposId>("exposid"sv),
                                foundation::is<SpanKind::SeeBelow>("seebelow"sv),
                                foundation::is<SpanKind::ImplDefined>("impl-defined"sv),
                                foundation::is<SpanKind::Placeholder>("placeholder"sv),
                                foundation::is<SpanKind::Ref>("ref"sv),
                                foundation::is<SpanKind::LibraryIndex>("library-index"sv));

inline constexpr auto kIndexKindNames =
    foundation::table<IndexKind>(foundation::is<IndexKind::Global>("global"sv),
                                 foundation::is<IndexKind::Constructor>("ctor"sv),
                                 foundation::is<IndexKind::Destructor>("dtor"sv),
                                 foundation::is<IndexKind::Member>("member"sv),
                                 foundation::is<IndexKind::MemberX>("memberx"sv),
                                 foundation::is<IndexKind::MemberExpos>("memberexpos"sv),
                                 foundation::is<IndexKind::Zombie>("zombie"sv),
                                 foundation::is<IndexKind::Misc>("misc"sv));

template <class E>
std::optional<E> enumerator_named(const foundation::total_table<E, std::string_view>& names, std::string_view name) {
    const auto found = std::ranges::find_if(foundation::enumerators<E>, [&](E e) { return names[e] == name; });
    return found != foundation::enumerators<E>.end() ? std::optional<E>{*found} : std::nullopt;
}

} // namespace

std::string_view        span_kind_name(SpanKind k) { return kSpanKindNames[k]; }
std::optional<SpanKind> span_kind_from_name(std::string_view s) { return enumerator_named(kSpanKindNames, s); }

std::string_view         index_kind_name(IndexKind k) { return kIndexKindNames[k]; }
std::optional<IndexKind> index_kind_from_name(std::string_view s) { return enumerator_named(kIndexKindNames, s); }

std::string_view           element_name(ElementKind k) { return kElementNames[k]; }
std::optional<ElementKind> element_from_name(std::string_view s) { return enumerator_named(kElementNames, s); }

std::string_view           disposition_name(Disposition d) { return kDispositionNames[d]; }
std::optional<Disposition> disposition_from_name(std::string_view s) { return enumerator_named(kDispositionNames, s); }

std::string_view          member_kind_name(MemberKind k) { return kMemberKindNames[k]; }
std::optional<MemberKind> member_kind_from_name(std::string_view s) { return enumerator_named(kMemberKindNames, s); }

void canonicalize(ItemDescr& d) {
    // Within one kind (design §5.2), a derived element sorts ahead of
    // an authored one ("derived conjuncts first, authored prose appended"),
    // so the secondary key is `!derived` -- `derived` sorts as 0, ahead of
    // an authored element's 1. Stable, so two elements that tie on both keys
    // (both derived, or both authored -- not produced today, but not ruled
    // out) keep their relative order.
    std::stable_sort(
        d.elements.begin(), d.elements.end(), [](const DescriptionElement& a, const DescriptionElement& b) {
            return std::pair(canonical_rank(a.kind), !a.derived) < std::pair(canonical_rank(b.kind), !b.derived);
        });
}

} // namespace beman::specgen::ir
