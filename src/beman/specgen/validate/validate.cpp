// src/beman/specgen/validate/validate.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// See validate.hpp for the design rationale (decision
// expected-error-taxonomy). `ValidationAlgebra` is a named visitor struct
// (decision visitation-rules: `ir::NodeF` has four alternatives, above the
// <=3-stateless-lambda threshold), one doc-commented case per alternative --
// mirroring `ir_fold.hpp`'s `NodeProjector` and `latex.cpp`'s
// `SeededProjector`.

#include <beman/specgen/validate/validate.hpp>

#include <beman/specgen/foundation/monoid.hpp>
#include <beman/specgen/foundation/overloaded.hpp>
#include <beman/specgen/ir_fold.hpp>

#include <beman/tree_algorithms/recursion_schemes.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::validate {

namespace foundation = beman::specgen::foundation;

namespace {

using beman::specgen::foundation::overloaded;

// The span-table validator: ir.hpp:42 documents
// `CodeText::spans` as "sorted by begin, non-overlapping", but nothing
// enforces it. `render_code_spans` (backend/common.hpp) silently
// `continue`s past any span that is out of range, inverted, or overlapping,
// so a malformed table renders as plausible wording with a span quietly
// missing, and no golden can see it -- this reports the same three defects
// instead of rendering around them.
Diagnostics check_spans(const std::string& context, const ir::CodeText& code) {
    // Per-span checks, independent of neighbors: an inverted range, or one
    // reaching past the end of the text.
    Diagnostics per_span =
        code.spans | std::views::enumerate | std::views::transform([&](const auto& entry) {
            const auto& [index, span] = entry;
            Diagnostics out;
            if (span.begin > span.end)
                out.push_back({Severity::Error,
                               context,
                               std::format("span {} is inverted: begin {} > end {}", index, span.begin, span.end)});
            if (span.end > code.text.size())
                out.push_back(
                    {Severity::Error,
                     context,
                     std::format("span {} end {} exceeds text length {}", index, span.end, code.text.size())});
            return out;
        }) |
        std::views::join | std::ranges::to<std::vector>();

    // Adjacent-pair check: sortedness and non-overlap collapse into the same
    // comparison -- a span that starts before its predecessor ends is
    // either out of order or overlapping, and it does not matter which:
    // either way the table cannot be walked left to right the way
    // render_code_spans (backend/common.hpp) walks it.
    Diagnostics overlap =
        code.spans | std::views::pairwise | std::views::enumerate | std::views::filter([](const auto& entry) {
            const auto& [index, pair]      = entry;
            const auto& [previous, second] = pair;
            return second.begin < previous.end;
        }) |
        std::views::transform([&](const auto& entry) {
            const auto& [index, pair]      = entry;
            const auto& [previous, second] = pair;
            return Diagnostic{
                Severity::Error,
                context,
                std::format(
                    "span {} begins at {}, before span {} ends at {}", index + 1, second.begin, index, previous.end)};
        }) |
        std::ranges::to<std::vector>();

    return diagnostics_monoid.combine(std::move(per_span), overlap);
}

// --- the coverage invariant (design §9) -------------------------------------

// The stable name of every `ir::Section` in a node and its descendants.
//
// A pre-pass rather than a case of the main algebra, because "is there a
// section called `foo.bar`" is a question about the whole document and the
// main fold's carrier is `Diagnostics` -- a bottom-up fold to findings cannot
// answer it, and widening the carrier to answer it would make every case pay
// for one check. Two cheap folds beat one expensive one, and this is the same
// pre-pass shape the front end already uses for its marker sets.
const auto names_monoid = foundation::monoid{
    [](std::set<std::string> a, const std::set<std::string>& b) {
        a.insert(b.begin(), b.end());
        return a;
    },
    std::set<std::string>{},
};

std::set<std::string> section_names_layer(const ir::NodeF<std::set<std::string>>& layer) {
    return std::visit(overloaded{
                          [](const ir::SectionF<std::set<std::string>>& s) {
                              std::set<std::string> names = foundation::mconcat(s.children, names_monoid);
                              if (!s.stable_name.empty())
                                  names.insert(s.stable_name);
                              return names;
                          },
                          // Nothing but a Section carries a stable name; a
                          // lambda per leaf rather than a catch-all `auto` so
                          // an alternative added to ir::NodeF stops compiling
                          // here (decision visitation-rules) instead of
                          // silently contributing nothing.
                          [](const ir::Synopsis&) { return std::set<std::string>{}; },
                          [](const ir::SpecItem&) { return std::set<std::string>{}; },
                          [](const ir::FreeParagraph&) { return std::set<std::string>{}; },
                      },
                      layer);
}

std::set<std::string> section_names(const ir::Node& node) {
    return beman::tree_algorithms::fold_with<std::set<std::string>>(
        section_names_layer, ir::node_fmap, ir::node_project, node);
}

// Design §9's coverage invariant, applied to one synopsis's roster
// (ir.hpp's SynopsisEntry -- the front end's record of what became of each
// class-body declaration). Both directions are hard errors there, and both
// are reported here:
//
//  - Direction 1: a declaration accounted for by nothing. Every other
//    disposition *is* an accounting -- described, `\merge`d, `\omit`ted,
//    defaulted/deleted, exposition-only, or an unmarked private member that
//    design §6 treats as exposition rather than interface -- so
//    `Undocumented` is exactly the residue §9 forbids.
//  - Direction 2: markup that resolves to no declaration. The shape this
//    takes in practice is the inverse: the *declaration* is fine and the
//    wording written for it was routed -- by `\at`, or by the enclosing
//    `\ref` group -- somewhere that does not exist. Two ways to not exist,
//    both reported: a named target no `\rSec` in this document opens, and no
//    target at all (an in-class member sitting under no `\ref` group and
//    carrying no `\at`, whose `section` is therefore empty).
//    document_build::build_tree drops such an item silently, so without the
//    roster entry there is nothing left to see -- which is why this check
//    cannot be written against the wording alone.
Diagnostics check_coverage(const std::string&                    context,
                           const std::vector<ir::SynopsisEntry>& roster,
                           const std::set<std::string>&          sections) {
    return foundation::mconcat_map(
        roster,
        [&](const ir::SynopsisEntry& entry) {
            Diagnostics out;
            if (entry.disposition == ir::Disposition::Undocumented)
                out.push_back({Severity::Error,
                               context,
                               "`" + entry.name +
                                   "` is declared in the synopsis but is not described, `\\omit`ted, `\\merge`d, or "
                                   "defaulted"});
            if (entry.disposition == ir::Disposition::Routed && entry.section.empty())
                out.push_back({Severity::Error,
                               context,
                               "the description of `" + entry.name +
                                   "` is routed to no section: it is written in the class body under no "
                                   "`\\ref` group and carries no `\\at`"});
            else if (entry.disposition == ir::Disposition::Routed && !sections.contains(entry.section))
                out.push_back({Severity::Error,
                               context,
                               "the description of `" + entry.name + "` is routed to [" + entry.section +
                                   "], which is not a section in this document"});
            return out;
        },
        diagnostics_monoid);
}

// --- the unmarked-private-data nudge (design §6, §9) ------------------------

// design §6's entity table gives unmarked private *functions* and unmarked
// private *data* different treatments: both are omitted from the synopsis,
// but only the data earns a "state not marked `\expos`" nudge. The asymmetry
// is about what the wording needs to be able to say. A private helper is
// machinery the description never has to mention, and hiding it is the right
// outcome; a class's *state* is what Effects and Ensures are written in terms
// of, so private data that is invisible may be an oversight.
//
// **This fires only on a class that already marks something `\expos`**, which
// is narrower than §6's text, and deliberately. Read literally the rule
// nudges every unmarked private data member, and that turned out to fire on
// ten of the fifteen corpus headers -- every one of them a filler member
// (`int data_ = 0;`) that exists so a minimal fixture compiles, named by no
// wording anywhere. A lint that fires on every class with a private member is
// one readers learn to scroll past, and the fixits it proposes there would be
// false: those members are not exposition-only state. Requiring an existing
// `\expos` turns the absence of a mark into an *inconsistency* -- this author
// is using the exposition-only mechanism and left this member out of it --
// which is the version of the signal that is usually a real oversight. Same
// conservative direction as the leakage checker and the drift check: a
// miss costs a finding, never invents one.
//
// It is a Note either way: a genuine implementation detail -- a cached hash,
// a spare bit -- is entitled to stay hidden even in a class that exposes the
// rest of its state. The complementary case is already an Error and belongs
// to the leakage checker: if the wording *does* name the member, `check_leakage` reports it,
// because then the reader demonstrably cannot follow the description. This
// nudge covers the same mistake made silently, where nothing in the wording
// points at the gap.
Diagnostics check_private_data(const std::string& context, const std::vector<ir::SynopsisEntry>& roster) {
    const bool exposes_state = std::ranges::any_of(
        roster, [](const ir::SynopsisEntry& e) { return e.disposition == ir::Disposition::Expos; });
    if (!exposes_state)
        return {};

    return foundation::mconcat_map(
        roster,
        [&](const ir::SynopsisEntry& entry) -> Diagnostics {
            if (entry.disposition != ir::Disposition::Private || entry.kind != ir::MemberKind::Data)
                return {};
            return {{Severity::Note,
                     context,
                     "`" + entry.name +
                         "` is unmarked private data in a class that marks other members `\\expos`, so it is "
                         "dropped from the synopsis: mark it `\\expos` too if the wording describes this class's "
                         "state in terms of it"}};
        },
        diagnostics_monoid);
}

// --- the leakage checker (design §9) ----------------------------------------

// What the document's rosters say about a bare identifier: whether some
// declaration of that name is visible to a reader of the wording, and -- if
// none is -- why the one the front end saw is not.
//
// The two halves come out of the *same* roster the coverage check reads, but
// they partition it differently, and the difference is the point. Coverage
// asks "is this declaration accounted for?", so `\omit`ted, `\merge`d and
// unmarked-private all count as accounted for. Leakage asks "can the reader
// see this name?", and those same three cannot: an `\omit`ted or `\merge`d
// member is not in the synopsis at all, and an unmarked private one is
// dropped from it (design §6). `\expos` is the one disposition that is
// invisible to coverage's question and visible to this one -- it is exactly
// the escape hatch §9's fixit trichotomy offers.
// What the front end saw of a declaration the reader cannot see: why it is
// invisible (the diagnostic says so) and what sort of declaration it was
// (check_unextracted_uses reports only a hidden *function*).
struct HiddenEntry {
    ir::Disposition disposition = ir::Disposition::Undocumented;
    ir::MemberKind  kind        = ir::MemberKind::Function;
};

struct NameVisibility {
    // Names with at least one visible declaration. A class's own name lands
    // here too (a synopsis is the reader's view of it), which is what keeps
    // `nullopt_t` -- a documented class whose *constructor* is `\omit`ted,
    // and so shares its name with an invisible roster entry -- from reading
    // as a leak everywhere the type is named.
    std::set<std::string> documented;
    // Names with no visible declaration, mapped to what the first invisible
    // one seen was. Consulted only after `documented` misses.
    std::map<std::string, HiddenEntry> hidden;
    // Namespaces whose qualifier survived the front end's
    // reference-resolved mapping (ir::ForeignNamespace), name as written
    // mapped to what it resolved to. Not a partition of any roster -- no
    // roster records a namespace -- but the same question answered about a
    // different kind of name, so it rides the same struct: `detail` in
    // rendered output is a name the reader cannot see, for a reason the
    // dispositions cannot express. Seeded from the document (see
    // validate(Document) below), which is the only place it can come from:
    // the nodes carry no trace of it.
    std::map<std::string, std::string> foreign;
};

const auto visibility_monoid = foundation::monoid{
    [](NameVisibility a, const NameVisibility& b) {
        a.documented.insert(b.documented.begin(), b.documented.end());
        // map::insert leaves an existing key alone, so the leftmost -- and
        // therefore document-order-first -- disposition wins.
        a.hidden.insert(b.hidden.begin(), b.hidden.end());
        a.foreign.insert(b.foreign.begin(), b.foreign.end());
        return a;
    },
    NameVisibility{},
};

// Can a reader of the generated wording see a declaration with this
// disposition? A switch over every enumerator rather than a set of the four
// (decision visitation-rules): a disposition added to ir.hpp should stop
// compiling here, because "is it visible" is not a question a new one can be
// assumed to answer either way.
bool names_a_visible_entity(ir::Disposition disposition) {
    switch (disposition) {
    case ir::Disposition::Described:
    case ir::Disposition::Routed:
    case ir::Disposition::Defaulted: // in the synopsis, just without wording
    case ir::Disposition::Expos:     // §9's own escape hatch
        return true;
    case ir::Disposition::Merged:
    case ir::Disposition::Omitted:
    case ir::Disposition::Private:
    case ir::Disposition::Undocumented:
        return false;
    }
    return false;
}

// Why a name the reader cannot see is not visible, as the diagnostic says it.
std::string_view invisibility_reason(ir::Disposition disposition) {
    switch (disposition) {
    case ir::Disposition::Merged:
        return "a `\\merge`d twin";
    case ir::Disposition::Omitted:
        return "`\\omit`ted";
    case ir::Disposition::Private:
        return "an unmarked private member";
    case ir::Disposition::Undocumented:
        return "declared but never described";
    case ir::Disposition::Described:
    case ir::Disposition::Routed:
    case ir::Disposition::Defaulted:
    case ir::Disposition::Expos:
        return "visible"; // unreachable: names_a_visible_entity gates this
    }
    return "visible";
}

NameVisibility visibility_layer(const ir::NodeF<NameVisibility>& layer) {
    return std::visit(
        overloaded{
            [](const ir::SectionF<NameVisibility>& s) { return foundation::mconcat(s.children, visibility_monoid); },
            [](const ir::Synopsis& v) {
                NameVisibility out = foundation::mconcat_map(
                    v.roster,
                    [](const ir::SynopsisEntry& entry) {
                        NameVisibility one;
                        if (names_a_visible_entity(entry.disposition))
                            one.documented.insert(entry.name);
                        else
                            one.hidden.emplace(entry.name, HiddenEntry{entry.disposition, entry.kind});
                        return one;
                    },
                    visibility_monoid);
                if (!v.name.empty())
                    out.documented.insert(v.name);
                return out;
            },
            // Only a roster records what became of a name; an
            // item and a paragraph are where names are *used*,
            // which is the other side of this check.
            [](const ir::SpecItem&) { return NameVisibility{}; },
            [](const ir::FreeParagraph&) { return NameVisibility{}; },
        },
        layer);
}

NameVisibility name_visibility(const ir::Node& node) {
    return beman::tree_algorithms::fold_with<NameVisibility>(visibility_layer, ir::node_fmap, ir::node_project, node);
}

struct IdentifierRun {
    std::string name;
    bool        namespace_qualifier = false;
};

// Every maximal run of identifier characters in @p text, in order of
// appearance, plus whether that occurrence is followed by `::`. Not a C++
// lexer: a run inside a string literal or a comment is returned like any
// other, and a qualified name arrives as its separate components (`detail`,
// `storage`). Roster checks need every run; foreign-namespace checks need the
// occurrence-level qualifier bit so an English use of `detail` is not
// mistaken for `detail::`.
std::vector<IdentifierRun> identifier_runs(std::string_view text) {
    const auto is_identifier_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    std::vector<IdentifierRun> runs;
    std::size_t                pos = 0;
    // substrate generic algorithm: the position-tracking scan is what keeps
    // the punctuation following each identifier occurrence available.
    while (pos < text.size()) {
        if (!is_identifier_char(text[pos])) {
            ++pos;
            continue;
        }
        const std::size_t begin = pos;
        // substrate generic algorithm: consume this identifier run.
        while (pos < text.size() && is_identifier_char(text[pos]))
            ++pos;
        std::size_t after = pos;
        // substrate generic algorithm: find the following nonblank token.
        while (after < text.size() && (text[after] == ' ' || text[after] == '\t'))
            ++after;
        runs.push_back(
            IdentifierRun{std::string(text.substr(begin, pos - begin)), text.substr(after).starts_with("::")});
    }
    return runs;
}

// What a surviving namespace qualifier reads as. `detail::storage` in
// rendered output names an entity in a namespace the specification does not
// have, and no fixit the roster half offers applies to a namespace -- there
// is nothing to mark `\expos`, and the name is not the author's to demote --
// so this message names its own two.
//
// `documented` still wins, for the same reason it does below: a class named
// like a namespace is the reader's view of itself.
std::optional<std::string> foreign_message(const IdentifierRun& run, const NameVisibility& visible) {
    const auto entry = visible.foreign.find(run.name);
    if (!run.namespace_qualifier || entry == visible.foreign.end() || visible.documented.contains(run.name))
        return std::nullopt;
    return "`" + run.name +
           "` appears in rendered output but is not a documented entity (a namespace qualifier resolving "
           "to `" +
           entry->second +
           "`): rewrite the name in documented terms, or move the entity it names into the specified "
           "namespace";
}

// Design §9's leakage checker over one name: everything in rendered output
// must resolve to something the reader can see. Two of §9's four resolutions
// are answerable here -- the roster says whether a class-body declaration is
// visible, and `foreign` says a qualifier resolved outside the
// specification -- and the message says which one failed.
//
// A name matching neither is left alone: it may be a std entity, a template
// parameter, or a local of the extracted body, and as bare text those are
// indistinguishable from each other. That is what keeps the imprecision of
// `identifier_runs` one-directional -- it can cost a finding, never invent
// one.
std::optional<std::string> leak_message(const IdentifierRun& run, const NameVisibility& visible) {
    if (!visible.documented.contains(run.name) && visible.hidden.contains(run.name))
        return "`" + run.name + "` is used in wording but is not a documented entity (" +
               std::string(invisibility_reason(visible.hidden.at(run.name).disposition)) +
               "): mark it `\\expos`, rewrite the wording in documented terms, or demote it to authored prose";
    return foreign_message(run, visible);
}

// Every name in @p code that @p classify calls a leak, as the message it
// returns for that name.
//
// One finding per leaked name per fragment, at its first appearance: a body
// assigning `value_` twice has one problem, not two.
template <typename Classify>
Diagnostics report_leaks(const std::string& context, const ir::CodeText& code, Classify classify) {
    const auto name_of = [](const auto& leak) { return leak.first; };

    const std::vector<std::pair<std::string, std::optional<std::string>>> leaked =
        identifier_runs(code.text) |
        std::views::transform([&classify](const IdentifierRun& run) { return std::pair{run.name, classify(run)}; }) |
        std::views::filter([](const auto& leak) { return leak.second.has_value(); }) | std::ranges::to<std::vector>();

    return leaked | std::views::enumerate | std::views::filter([&leaked, &name_of](const auto& entry) {
               const auto& [index, leak] = entry;
               return std::ranges::find(leaked, leak.first, name_of) == leaked.begin() + index;
           }) |
           std::views::transform([&context](const auto& entry) {
               const auto& [index, leak] = entry;
               return Diagnostic{Severity::Error, context, *leak.second};
           }) |
           std::ranges::to<Diagnostics>();
}

// One fragment of wording, against both resolutions.
Diagnostics check_leakage(const std::string& context, const ir::CodeText& code, const NameVisibility& visible) {
    return report_leaks(context, code, [&visible](const IdentifierRun& run) { return leak_message(run, visible); });
}

// A synopsis, against the qualifier resolution plus the one hidden
// disposition that cannot double-report there: Private. The full roster half
// must not run here: a synopsis is where names are *declared*, so a `\merge`d
// twin -- invisible, and its name in the synopsis text by construction --
// would be reported at its own declaration in every class that has one. An
// unmarked private member has no such double life: its declaration is dropped
// from the synopsis unconditionally (design §6), so an occurrence of its name
// there is a *surviving* declaration reaching for it -- `using type = raw;`
// naming the elided private alias (issue #7), published wording that will not
// compile if copied. A surviving qualifier has
// no such double life either; it is wrong in the synopsis for exactly the reason it
// is wrong in an *Equivalent to:* body, and `spec_namespace.hpp` is where it
// happens to land.
Diagnostics
check_synopsis_leakage(const std::string& context, const ir::CodeText& code, const NameVisibility& visible) {
    return report_leaks(context, code, [&visible](const IdentifierRun& run) -> std::optional<std::string> {
        if (!visible.documented.contains(run.name) && visible.hidden.contains(run.name) &&
            visible.hidden.at(run.name).disposition == ir::Disposition::Private)
            return "`" + run.name + "` is named in the synopsis but is not a visible declaration (" +
                   std::string(invisibility_reason(ir::Disposition::Private)) +
                   "): mark it `\\expos` so it can be named, or mask the declaration that names it (bare "
                   "`\\seebelow`, or `\\impdef` on an alias)";
        return foreign_message(run, visible);
    });
}

// Every `CodeText` a paragraph renders as code -- the backticked spans that
// become `\tcode`. `RefInline` and `ConceptRef` name a stable name and a
// library concept respectively, neither of which is a class-body
// declaration, so neither is a leakage site.
std::vector<const ir::CodeText*> paragraph_code(const ir::Paragraph& paragraph) {
    return paragraph | std::views::transform([](const ir::Inline& inl) {
               const auto* code = std::get_if<ir::CodeInline>(&inl);
               return code != nullptr ? &code->code : nullptr;
           }) |
           std::views::filter([](const ir::CodeText* code) { return code != nullptr; }) |
           std::ranges::to<std::vector>();
}

Diagnostics check_paragraph_spans(const std::string& context, const ir::Paragraph& paragraph) {
    return foundation::mconcat_map(
        paragraph_code(paragraph),
        [&](const ir::CodeText* code) { return check_spans(context, *code); },
        diagnostics_monoid);
}

Diagnostics
check_paragraph_leakage(const std::string& context, const ir::Paragraph& paragraph, const NameVisibility& visible) {
    return foundation::mconcat_map(
        paragraph_code(paragraph),
        [&](const ir::CodeText* code) { return check_leakage(context, *code, visible); },
        diagnostics_monoid);
}

std::vector<const ir::Paragraph*> table_paragraphs(const ir::Table2D& table) {
    std::vector<const ir::Paragraph*> out{&table.caption, &table.column1, &table.column2};
    out.append_range(table.rows | std::views::transform([](const ir::Table2DRow& row) {
                         return std::array{&row.header, &row.cell1, &row.cell2};
                     }) |
                     std::views::join);
    return out;
}

bool paragraph_has_content(const ir::Paragraph& paragraph) {
    const auto has_non_whitespace = [](std::string_view text) {
        return std::ranges::any_of(text, [](char ch) { return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r'; });
    };
    return std::ranges::any_of(paragraph, [&](const ir::Inline& inl) {
        return std::visit(overloaded{
                              [&](const ir::TextInline& text) { return has_non_whitespace(text.text); },
                              [&](const ir::CodeInline& code) { return has_non_whitespace(code.code.text); },
                              [&](const ir::RefInline& ref) { return has_non_whitespace(ref.stable_name); },
                              [&](const ir::ConceptRef& ref) { return has_non_whitespace(ref.name); },
                          },
                          inl);
    });
}

Diagnostics check_table_structure(const ir::DescriptionElement& element) {
    if (!element.table)
        return {};

    const ir::Table2D& table   = *element.table;
    const std::string  context = std::string(ir::element_name(element.kind)) + "/table";
    Diagnostics        out;
    const auto         require = [&](bool condition, std::string message) {
        if (!condition)
            out.push_back({Severity::Error, context, std::move(message)});
    };

    require(paragraph_has_content({ir::TextInline{table.stable_name}}), "table stable name is empty");
    require(paragraph_has_content(table.caption), "table caption is empty");
    require(paragraph_has_content(table.column1), "table first column header is empty");
    require(paragraph_has_content(table.column2), "table second column header is empty");
    require(!table.rows.empty(), "table requires at least one row");
    out.append_range(foundation::mconcat_map(
        table.rows | std::views::enumerate,
        [&](const auto& entry) {
            const auto& [index, row] = entry;
            Diagnostics row_findings;
            const auto  require_row = [&](bool condition, std::string message) {
                if (!condition)
                    row_findings.push_back({Severity::Error, context, std::move(message)});
            };
            require_row(paragraph_has_content(row.header), std::format("table row {} header is empty", index + 1));
            require_row(paragraph_has_content(row.cell1), std::format("table row {} first cell is empty", index + 1));
            require_row(paragraph_has_content(row.cell2), std::format("table row {} second cell is empty", index + 1));
            return row_findings;
        },
        diagnostics_monoid));
    return out;
}

Diagnostics check_element_spans(const ir::DescriptionElement& element) {
    const std::string context = std::string(ir::element_name(element.kind));
    Diagnostics       out     = foundation::mconcat_map(
        element.paragraphs,
        [&](const ir::Paragraph& paragraph) { return check_paragraph_spans(context, paragraph); },
        diagnostics_monoid);
    if (element.itemize)
        out = diagnostics_monoid.combine(
            std::move(out),
            foundation::mconcat_map(
                element.itemize->items,
                [&](const ir::Paragraph& paragraph) { return check_paragraph_spans(context, paragraph); },
                diagnostics_monoid));
    if (element.table)
        out = diagnostics_monoid.combine(
            std::move(out),
            foundation::mconcat_map(
                table_paragraphs(*element.table),
                [&](const ir::Paragraph* paragraph) { return check_paragraph_spans(context, *paragraph); },
                diagnostics_monoid));
    if (element.equivalent)
        out = diagnostics_monoid.combine(std::move(out), check_spans(context + "-equiv", element.equivalent->code));
    return out;
}

// One description element's wording: its prose, its itemized conditions, and
// its extracted "Equivalent to:" body. The body gets its own context suffix
// -- `effects-equiv` -- because that is the marker the author would go and
// edit, and because it is the one fragment here that was extracted from code
// rather than written.
Diagnostics check_element_leakage(const ir::DescriptionElement& element, const NameVisibility& visible) {
    const std::string context = std::string(ir::element_name(element.kind));

    Diagnostics prose = foundation::mconcat_map(
        element.paragraphs,
        [&](const ir::Paragraph& paragraph) { return check_paragraph_leakage(context, paragraph, visible); },
        diagnostics_monoid);

    Diagnostics itemized =
        element.itemize.has_value()
            ? foundation::mconcat_map(
                  element.itemize->items,
                  [&](const ir::Paragraph& paragraph) { return check_paragraph_leakage(context, paragraph, visible); },
                  diagnostics_monoid)
            : Diagnostics{};

    Diagnostics table = element.table.has_value()
                            ? foundation::mconcat_map(
                                  table_paragraphs(*element.table),
                                  [&](const ir::Paragraph* paragraph) {
                                      return check_paragraph_leakage(context, *paragraph, visible);
                                  },
                                  diagnostics_monoid)
                            : Diagnostics{};

    Diagnostics equivalent = element.equivalent.has_value()
                                 ? check_leakage(context + "-equiv", element.equivalent->code, visible)
                                 : Diagnostics{};

    return diagnostics_monoid.combine(
        diagnostics_monoid.combine(diagnostics_monoid.combine(std::move(prose), itemized), table), equivalent);
}

// --- a helper named only by a body the tool never renders (design §9) --------

// Every identifier in the fragments `check_leakage` runs over, and
// deliberately not the ones it does not: an itemdecl signature, an element's
// prose, its itemized conditions and its "Equivalent to:" body, and a free
// paragraph -- but not a synopsis, for the same reason the roster half skips
// one there (a synopsis is where names are *declared*).
//
// This is the "only" in design §9's note clause, made checkable: a helper
// named in wording is the **error** case and `check_leakage` has already
// reported it, so the note must not fire on it a second time. Keeping the two
// lists in step is a real coupling -- a fragment added to `check_leakage` and
// not to this fold would produce both findings for one name -- and it is why
// this is written as a parallel fold rather than as a set the algebra
// happens to accumulate.
std::set<std::string> code_names(const ir::CodeText& code) {
    return identifier_runs(code.text) | std::views::transform(&IdentifierRun::name) |
           std::ranges::to<std::set<std::string>>();
}

std::set<std::string> paragraph_names(const ir::Paragraph& paragraph) {
    return foundation::mconcat_map(
        paragraph_code(paragraph), [](const ir::CodeText* code) { return code_names(*code); }, names_monoid);
}

std::set<std::string> element_names(const ir::DescriptionElement& element) {
    std::set<std::string> names = foundation::mconcat_map(element.paragraphs, paragraph_names, names_monoid);
    if (element.itemize)
        names = names_monoid.combine(std::move(names),
                                     foundation::mconcat_map(element.itemize->items, paragraph_names, names_monoid));
    if (element.table)
        names = names_monoid.combine(std::move(names),
                                     foundation::mconcat_map(
                                         table_paragraphs(*element.table),
                                         [](const ir::Paragraph* paragraph) { return paragraph_names(*paragraph); },
                                         names_monoid));
    if (element.equivalent)
        names = names_monoid.combine(std::move(names), code_names(element.equivalent->code));
    return names;
}

std::set<std::string> wording_names_layer(const ir::NodeF<std::set<std::string>>& layer) {
    return std::visit(
        overloaded{
            [](const ir::SectionF<std::set<std::string>>& s) { return foundation::mconcat(s.children, names_monoid); },
            // Not a wording site (see above), and the one case here whose
            // emptiness is a decision rather than a fact.
            [](const ir::Synopsis&) { return std::set<std::string>{}; },
            [](const ir::SpecItem& v) {
                return names_monoid.combine(foundation::mconcat_map(v.decl.signatures, code_names, names_monoid),
                                            foundation::mconcat_map(v.descr.elements, element_names, names_monoid));
            },
            [](const ir::FreeParagraph& v) { return paragraph_names(v.text); },
        },
        layer);
}

std::set<std::string> wording_names(const ir::Node& node) {
    return beman::tree_algorithms::fold_with<std::set<std::string>>(
        wording_names_layer, ir::node_fmap, ir::node_project, node);
}

// design §9's leakage checker at its *note* severity: an undocumented helper
// that "appears only in non-extracted bodies". The front end recorded each
// (documented function whose body is never rendered, member that body names)
// pair (ir::BodyUse); everything left is a question the roster answers, so the
// rule is Tier A like the other three halves of this check.
//
// Four filters, and the fourth is the one worth arguing about:
//
//   - a name with a visible declaration is fine, which is the ordinary case:
//     a body naturally calls the class's own public members;
//   - a name no roster holds is not judged at all -- the same silence the
//     roster half keeps, and for the same reason (a local, a parameter and a
//     `std::` name are indistinguishable from each other as bare text);
//   - a name the wording already leaked is left to `check_leakage`, which
//     reports it as an **Error**. That is §9's "only", and it is why this
//     needs `wording_names` above rather than just the roster;
//   - **only a hidden *function* is reported**, never hidden data.
//
// The last filter is narrower than "every hidden name" and is this
// rule's one real decision. §9 says *helper*, and a helper is a
// function: the fixit trichotomy makes sense for one (mark it `\expos`,
// rewrite the wording, demote it to prose) and the case for hidden *data*
// already belongs to a rule of its own -- design §6's private-data nudge
// (check_private_data above), which fires on the declaration rather
// than on each body that touches it. Reported both ways, the corpus's filler
// members (`int data_ = 0;`, read by the one accessor documented above them)
// would produce a note per *body* on top of the nudge's note per member, which
// is the same "a lint readers learn to scroll past" the nudge narrowed itself
// to avoid -- worse, in fact, since a member read by three bodies is reported
// three times. Measured over the corpus the split is again exact: dropping
// this filter reports **seventeen findings across eleven of the seventeen
// headers**, every one of them a filler data member no wording names, and
// keeping it reports **none**, which leaves the rule saying nothing until a
// header does the thing §9 is describing.
Diagnostics check_unextracted_uses(const std::vector<ir::BodyUse>& uses,
                                   const NameVisibility&           visible,
                                   const std::set<std::string>&    wording) {
    return foundation::mconcat_map(
        uses,
        [&](const ir::BodyUse& use) -> Diagnostics {
            if (visible.documented.contains(use.member) || wording.contains(use.member))
                return {};
            const auto hidden = visible.hidden.find(use.member);
            if (hidden == visible.hidden.end() || hidden->second.kind != ir::MemberKind::Function)
                return {};
            return {{Severity::Note,
                     use.function + "/body",
                     "`" + use.member + "` is used by this body but is not a documented entity (" +
                         std::string(invisibility_reason(hidden->second.disposition)) +
                         "); no wording names it, so nothing leaks yet: mark it `\\expos` before giving this body "
                         "an `\\effects-equiv`, or leave it as machinery the description does not name"}};
        },
        diagnostics_monoid);
}

// --- Mandates/Constraints drift (design §5.2) -------------------------------

// Which way a conjunct's subject reads: "`X` is `true`", "`X` is `false`",
// or neither (a concept-id, or the verbatim fallback) -- the two cases
// `phrase_conjunct` (frontend.cpp) never puts a trailing `true`/`false`
// literal onto.
enum class ConjunctPolarity { Positive, Negative, Unknown };

struct Conjunct {
    std::string      subject;
    ConjunctPolarity polarity;
};

// The subject and polarity of one derived conjunct, read off the
// `phrase_conjunct` shape it was built from: a bool trait/negation conjunct
// is `[CodeInline{subject}, TextInline{" is "}, CodeInline{"true"|"false"}]`,
// so the subject is always the first piece and, when the last piece is a
// `true`/`false` CodeInline, the polarity is right there too. A concept-id
// or verbatim-fallback conjunct has no such trailing literal, so it reads
// Unknown -- the drift check below skips those rather than guessing.
std::optional<Conjunct> read_conjunct(const ir::Paragraph& paragraph) {
    if (paragraph.empty())
        return std::nullopt;
    const auto* subject = std::get_if<ir::CodeInline>(&paragraph.front());
    if (subject == nullptr)
        return std::nullopt;
    ConjunctPolarity polarity = ConjunctPolarity::Unknown;
    if (const auto* tail = std::get_if<ir::CodeInline>(&paragraph.back())) {
        if (tail->code.text == "true")
            polarity = ConjunctPolarity::Positive;
        else if (tail->code.text == "false")
            polarity = ConjunctPolarity::Negative;
    }
    return Conjunct{subject->code.text, polarity};
}

// Does @p paragraph assert @p subject, and with what polarity, per the same
// `` `X` is `true`/`false` `` shape authors already write by hand (the
// corpus convention `spec_inclass_markers.hpp` uses)? Scans forward from a
// matching `CodeInline` for the next `CodeInline` within a few pieces --
// not the immediately next one, since a hand-written paragraph's "is" is
// its own `TextInline` between them -- and reads `true`/`false` off it.
// nullopt means the subject was not found at all; `Unknown` means it was
// found but nothing nearby reads as a polarity, which the caller treats as
// "found, but not comparable" rather than "not found".
std::optional<ConjunctPolarity> find_subject(const ir::Paragraph& paragraph, const std::string& subject) {
    const auto is_named = [&](const ir::Inline& piece) {
        const auto* code = std::get_if<ir::CodeInline>(&piece);
        return code != nullptr && code->code.text == subject;
    };
    const auto match = std::ranges::find_if(paragraph, is_named);
    if (match == paragraph.end())
        return std::nullopt;

    // A bounded lookahead, not the rest of the paragraph: the polarity
    // literal belongs to *this* conjunct, and a paragraph joining several
    // conjuncts (conjuncts::join_sentence) has another subject's `true`/
    // `false` further along that must not be misread as this one's.
    constexpr std::ptrdiff_t window          = 3;
    const auto               window_end      = std::ranges::next(match, window + 1, paragraph.end());
    const auto               is_bool_literal = [](const ir::Inline& piece) {
        const auto* code = std::get_if<ir::CodeInline>(&piece);
        return code != nullptr && (code->code.text == "true" || code->code.text == "false");
    };
    const auto polarity = std::ranges::find_if(match + 1, window_end, is_bool_literal);
    if (polarity == window_end)
        return ConjunctPolarity::Unknown;
    return std::get_if<ir::CodeInline>(&*polarity)->code.text == "true" ? ConjunctPolarity::Positive
                                                                        : ConjunctPolarity::Negative;
}

// Every paragraph an authored element carries wording in: its prose plus
// its itemized conditions -- the same two places check_element_leakage
// (above) reads wording from.
std::vector<const ir::Paragraph*> element_paragraphs(const ir::DescriptionElement& element) {
    const auto                        address_of = [](const ir::Paragraph& p) { return &p; };
    std::vector<const ir::Paragraph*> out =
        element.paragraphs | std::views::transform(address_of) | std::ranges::to<std::vector>();
    if (element.itemize)
        out.append_range(element.itemize->items | std::views::transform(address_of));
    if (element.table)
        out.append_range(table_paragraphs(*element.table));
    return out;
}

// One derived/authored pair sharing a kind (design §5.2): for every derived
// conjunct with a known subject and polarity, look for that subject
// anywhere in the authored element's wording, and warn when found --
// "duplicates" at matching polarity, "contradicts" at the opposite one. A
// subject not found at all, or found with an unreadable polarity on either
// side, is silently not compared -- the same conservative direction the
// leakage checker set: a miss costs a finding, never invents one.
std::string_view polarity_spelling(ConjunctPolarity p) { return p == ConjunctPolarity::Positive ? "true" : "false"; }

Diagnostics check_drift_pair(const ir::DescriptionElement& derived, const ir::DescriptionElement& authored) {
    const std::vector<const ir::Paragraph*> authored_paragraphs = element_paragraphs(authored);
    const std::string                       kind_name           = std::string(ir::element_name(derived.kind));

    return foundation::mconcat_map(
        derived.conjuncts,
        [&](const ir::Paragraph& conjunct_paragraph) -> Diagnostics {
            const std::optional<Conjunct> conjunct = read_conjunct(conjunct_paragraph);
            if (!conjunct || conjunct->polarity == ConjunctPolarity::Unknown)
                return {};

            // The first authored paragraph with a *comparable* reading of
            // this subject -- one that is present and not Unknown; a
            // paragraph that merely mentions the subject without a nearby
            // true/false is not a comparison point (find_subject already
            // reads it as Unknown, not as "not found").
            const std::vector<std::optional<ConjunctPolarity>> readings =
                authored_paragraphs |
                std::views::transform([&](const ir::Paragraph* p) { return find_subject(*p, conjunct->subject); }) |
                std::ranges::to<std::vector>();
            const auto comparable = std::ranges::find_if(
                readings, [](const auto& r) { return r.has_value() && *r != ConjunctPolarity::Unknown; });
            if (comparable == readings.end())
                return {};

            if (**comparable == conjunct->polarity)
                return {
                    {Severity::Warning,
                     kind_name,
                     "the authored " + kind_name + " duplicates the derived `" + conjunct->subject + "` conjunct"}};
            return {{Severity::Warning,
                     kind_name,
                     "the authored " + kind_name + " contradicts the derived `" + conjunct->subject +
                         "` conjunct (derived `" + std::string(polarity_spelling(conjunct->polarity)) +
                         "`, authored `" + std::string(polarity_spelling(**comparable)) + "`)"}};
        },
        diagnostics_monoid);
}

// The front end keeps suppressed derived conjuncts on the authored element
// itself, so compare that element's rendered prose with its own validator-only
// evidence. The grouped authored/derived form remains readable for hand-built
// IR, though the front end does not emit it.
Diagnostics check_mandates_constraints_drift(const std::vector<ir::DescriptionElement>& elements) {
    // Materialized rather than folded straight over the chunk_by view:
    // chunk_by_view caches its current group boundary in the iterator, so it
    // is only range-for-able as a mutable range, and mconcat_map (like
    // ir_fold's other consumers) takes its range by const reference.
    const std::vector<std::vector<ir::DescriptionElement>> groups =
        elements | std::views::chunk_by([](const auto& a, const auto& b) { return a.kind == b.kind; }) |
        std::views::transform([](auto&& group) { return group | std::ranges::to<std::vector>(); }) |
        std::ranges::to<std::vector>();

    return foundation::mconcat_map(
        groups,
        [](const std::vector<ir::DescriptionElement>& group) -> Diagnostics {
            const auto derived_it  = std::ranges::find_if(group, [](const auto& e) { return e.derived; });
            const auto authored_it = std::ranges::find_if(group, [](const auto& e) { return !e.derived; });
            if (authored_it != group.end() && !authored_it->conjuncts.empty())
                return check_drift_pair(*authored_it, *authored_it);
            if (derived_it == group.end() || authored_it == group.end())
                return {};
            return check_drift_pair(*derived_it, *authored_it);
        },
        diagnostics_monoid);
}

// --- noexcept <-> *Throws:* cross-check (design §5.4) -----------------------

// What a signature's exception specification says. Design §5.4 keeps
// `noexcept` in the signature and derives no *Throws:* from it; this is the
// reading the validator does instead.
//
// `noexcept(false)` reads as `None`, not as a third state: it says the
// function may throw, which is what an absent specification says too, and
// every use below asks only "can this throw?".
enum class NoexceptSpec {
    None,          // no `noexcept`, or `noexcept(false)`
    Unconditional, // `noexcept`, or `noexcept(true)`
    Conditional,   // `noexcept(expr)` -- may or may not throw, so not comparable
};

// Bracket depth immediately *before* each character of @p text, counting
// `(`, `[` and `{` alike. Not a C++ lexer, for the same reason
// `identifier_runs` (above) is not: a bracket inside a string literal or a
// comment counts, and a `<` never does. A declaration carries neither, and
// the one consumer below only ever uses this to *ignore* things, so the
// failure mode of imprecision stays a missed finding rather than a false one.
int bracket_delta(char c) {
    if (c == '(' || c == '[' || c == '{')
        return 1;
    if (c == ')' || c == ']' || c == '}')
        return -1;
    return 0;
}

std::vector<int> bracket_depths(std::string_view text) {
    std::vector<int> depths;
    depths.reserve(text.size());
    std::transform_exclusive_scan(text.begin(), text.end(), std::back_inserter(depths), 0, std::plus{}, bracket_delta);
    return depths;
}

// Every maximal identifier run in @p text, paired with its byte offset --
// `identifier_runs` (above) with the offsets kept, which this needs because a
// `noexcept` token is only meaningful together with what follows it.
std::vector<std::pair<std::size_t, std::string_view>> identifier_positions(std::string_view text) {
    const auto is_identifier_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    return text | std::views::chunk_by([is_identifier_char](char a, char b) {
               return is_identifier_char(a) == is_identifier_char(b);
           }) |
           std::views::filter([is_identifier_char](const auto& run) { return is_identifier_char(run.front()); }) |
           std::views::transform([text](const auto& run) {
               return std::pair{static_cast<std::size_t>(run.begin() - text.begin()),
                                std::string_view(run.begin(), run.end())};
           }) |
           std::ranges::to<std::vector>();
}

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\n");
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\n") - first + 1);
}

// Read @p signature's exception specification. Only a `noexcept` at bracket
// depth 0 is the *function's* -- one inside the parameter list belongs to a
// function-pointer parameter (`void f(void (*p)() noexcept)`), and one inside
// a `noexcept(...)` argument is the operator, not the specifier.
NoexceptSpec read_noexcept(std::string_view signature) {
    const std::vector<int> depths = bracket_depths(signature);
    const auto             tokens = identifier_positions(signature);
    const auto             found  = std::ranges::find_if(tokens, [&](const auto& token) {
        const auto& [offset, text] = token;
        return text == "noexcept" && depths[offset] == 0;
    });
    if (found == tokens.end())
        return NoexceptSpec::None;

    // Bare `noexcept` unless an argument list follows it.
    const std::string_view rest = trimmed(signature.substr(found->first + found->second.size()));
    if (!rest.starts_with('('))
        return NoexceptSpec::Unconditional;

    // The matching `)` is the first character after the `(` that the running
    // depth is back to 0 *after* -- `depths` holds the depth before each
    // character, so the closer's own delta has to be added back in.
    const std::size_t open      = static_cast<std::size_t>(rest.data() - signature.data());
    const auto        remainder = std::views::iota(open + 1, signature.size());
    const auto        close =
        std::ranges::find_if(remainder, [&](std::size_t i) { return depths[i] + bracket_delta(signature[i]) == 0; });
    if (close == std::ranges::end(remainder))
        return NoexceptSpec::Conditional; // unbalanced; not something to judge on

    const std::string_view argument = trimmed(signature.substr(open + 1, *close - open - 1));
    if (argument == "true")
        return NoexceptSpec::Unconditional;
    if (argument == "false")
        return NoexceptSpec::None;
    return NoexceptSpec::Conditional;
}

// Can *every* signature in this itemdecl be relied on not to throw? An
// itemdecl groups overloads (design §4.2), and a group whose members disagree
// -- or whose specification is conditional -- is one where an authored
// *Throws:* may be describing the overload that can throw, so the whole check
// stays silent on it. Same conservative direction as the leakage and drift
// checks: a mixed group costs a finding, never invents one.
bool every_signature_is_noexcept(const std::vector<ir::CodeText>& signatures) {
    return !signatures.empty() && std::ranges::all_of(signatures, [](const ir::CodeText& signature) {
        return read_noexcept(signature.text) == NoexceptSpec::Unconditional;
    });
}

// The text of a paragraph, with every inline flattened to what it names.
// Only ever compared against "Nothing" below, so the markup distinctions the
// backends draw between these four do not matter here.
std::string paragraph_text(const ir::Paragraph& paragraph) {
    return foundation::mconcat_map(
        paragraph,
        [](const ir::Inline& piece) {
            return std::visit(overloaded{
                                  [](const ir::TextInline& v) { return v.text; },
                                  [](const ir::CodeInline& v) { return v.code.text; },
                                  [](const ir::RefInline& v) { return v.stable_name; },
                                  [](const ir::ConceptRef& v) { return v.name; },
                              },
                              piece);
        },
        foundation::monoid{[](std::string a, const std::string& b) { return a += b; }, std::string{}});
}

// Does this *Throws:* element say the function throws nothing? "Throws:
// Nothing." is the draft's spelling of a no-throw guarantee, and it is the
// one authored *Throws:* that does not contradict a `noexcept` signature.
// An element carrying itemized conditions or an "Equivalent to:" body is
// saying more than that by construction.
bool reads_as_nothing(const ir::DescriptionElement& element) {
    if (element.itemize.has_value() || element.table.has_value() || element.equivalent.has_value())
        return false;
    const std::string joined = foundation::mconcat_map(
        element.paragraphs,
        paragraph_text,
        foundation::monoid{[](std::string a, const std::string& b) { return a += b; }, std::string{}});
    // Trailing punctuation and whitespace come off together: "Nothing.",
    // "Nothing" and " Nothing. " are the same claim.
    const auto             last = joined.find_last_not_of(" \t\n.");
    const std::string_view body =
        last == std::string::npos ? std::string_view{} : trimmed(std::string_view{joined}.substr(0, last + 1));
    return std::ranges::equal(body, std::string_view{"nothing"}, {}, [](char c) {
        return static_cast<char>(c | 0x20); // ASCII lowering; the word is ASCII
    });
}

// design §5.4's cross-check: the signature keeps the `noexcept`, and no
// *Throws:* is derived from it, so the only thing left to check is an
// *authored* one that says otherwise.
//
// **Both findings are Warnings, deliberately.** The contradiction is the
// stronger claim of the two -- one side of it is the declaration as the
// compiler sees it, so wording that says a `noexcept` function throws is not
// a difference of emphasis but false -- and Error would be the severity that
// claim deserves on its own. What decides it is what an Error *does*:
// `render --validate` (tools/specgen/main.cpp) skips rendering entirely once
// any Error is reported, so promoting this one would answer "your *Throws:*
// paragraph is wrong" by producing no wording at all. Suppressing a whole
// document over one paragraph is strictly worse than emitting it with the
// finding beside it, so this warns and renders. (That leaves the finding on
// stderr, which is easy to miss; marking a structural problem in the rendered
// output itself is a backend change, out of scope for the validator.)
//
// **There is deliberately no reverse direction.** "*Throws:* Nothing" does
// *not* imply the signature should be `noexcept`, so a check nudging that way
// would be wrong, not merely noisy. Under the Lakos Rule a narrow-contract
// function defaults to *not* `noexcept` even when its implementation visibly
// never throws, exactly so that checking the precondition and throwing stays in
// bounds -- with Contracts, and with deliberately limited UB, that
// is a live implementation freedom the wording must not foreclose. Hence the
// `NoexceptSpec::None` early return above is a permanent silence, not an
// unimplemented half.
Diagnostics check_noexcept_throws(const ir::SpecItem& item) {
    if (!every_signature_is_noexcept(item.decl.signatures))
        return {};

    // The finding names the declaration, as every other §9 rule names the
    // entity it is about. An item carries no name of its own -- only its
    // signatures -- and a section holding several `noexcept` members would
    // otherwise report two identical lines under one context. The first
    // signature stands for a grouped overload set: reaching here means every
    // signature in the group is `noexcept`, so any of them makes the point.
    const std::string subject = "`" + item.decl.signatures.front().text + "`";

    return foundation::mconcat_map(
        item.descr.elements,
        [&subject](const ir::DescriptionElement& element) -> Diagnostics {
            if (element.kind != ir::ElementKind::Throws)
                return {};
            if (element.paragraphs.empty() && !element.itemize && !element.table && !element.equivalent)
                return {}; // says nothing at all; nothing to contradict
            const std::string context = std::string(ir::element_name(element.kind));
            if (reads_as_nothing(element))
                return {{Severity::Warning,
                         context,
                         subject + " is declared `noexcept`, so the authored throws saying `Nothing` adds "
                                   "nothing: drop the element"}};
            return {{Severity::Warning,
                     context,
                     subject + " is declared `noexcept`, but the authored throws says it throws: correct the "
                               "wording, or take `noexcept` off the declaration"}};
        },
        diagnostics_monoid);
}

// --- a Ref span pointing at no section (design §7) -------------------------

// A synopsis `\ref` group header's stable name against the document's
// section set. The rule "every `\ref` group needs a matching `\rSec`" is
// only half covered by coverage: `check_coverage`'s
// `Disposition::Routed` case (above) checks exactly that, but only for an
// *in-class* member, whose wording is placed by the group header's name. An
// out-of-line definition's group header is not a routing instruction -- its
// wording is placed lexically (`Disposition::Described`) -- so it does not
// go through that check at all. What the header names is still read by a
// human, though: it renders straight into the synopsis as a `SpanKind::Ref`
// span, and this is the check that compares that payload to a section.
// Without it, a `// 22.5.3.3 Destructor[optional.dtor]` header
// that is not a `\rSec` marker silently absorbs everything after it into the
// previous section, and neither `generate` nor `render --validate` says
// anything.
//
// **Scoped to `ir::CodeText::spans` (a synopsis's own code) and to
// `SpanKind::Ref` only -- deliberately not `ir::RefInline`.** The two are
// different cross-references wearing similar names (AGENTS.md's `\ref`
// span vs. `\iref` inline). A synopsis's `\ref` group header is the group
// header of the class's *own* synopsis, and the draft's own group headers
// always name a subclause of the same clause the class is in -- so a payload
// that resolves nowhere in the generated document is a real defect. An
// `ir::RefInline` is prose, and prose legitimately cites subclauses outside
// the generated document all the time: `[optional]`'s own wording says
// "`val` is active\iref{class.union.general}", pointing at a core-language
// subclause that will never be a section in a generated fragment. Requiring
// every `RefInline` target to resolve locally would fire on correct draft
// wording, which is the worst kind of validator -- one that trains its
// reader to ignore it. So this reads `CodeText::spans`, never a `Paragraph`'s
// `RefInline`s.
Diagnostics
check_dangling_ref(const std::string& context, const ir::CodeText& code, const std::set<std::string>& sections) {
    return code.spans | std::views::filter([](const ir::Span& s) { return s.kind == ir::SpanKind::Ref; }) |
           std::views::filter([&](const ir::Span& s) { return !sections.contains(s.payload); }) |
           std::views::transform([&](const ir::Span& s) {
               return Diagnostic{Severity::Error,
                                 context,
                                 "the synopsis's `\\ref` group header names [" + s.payload +
                                     "], which is not a section in this document"};
           }) |
           std::ranges::to<Diagnostics>();
}

// --- an empty synopsis (design §9) ------------------------------------------

// A Synopsis whose code is empty renders as an empty fenced code block in
// every backend -- a blank box where a declaration should be, worse in a
// paper than no output at all. The front end no longer produces one (a
// record declaration that defines nothing becomes an ordinary itemdecl or no
// node at all), so an occurrence is hand-written IR or a front-end
// regression; either way it is an Error here so it cannot reach rendered
// output silently again.
Diagnostics check_nonempty(const std::string& context, const ir::CodeText& code) {
    if (!code.text.empty())
        return {};
    return {Diagnostic{Severity::Error, context, "the synopsis has no code and would render as an empty code block"}};
}

// `ValidationAlgebra`'s std::visit dispatch (decision visitation-rules: named
// struct, `ir::NodeF` has four alternatives).
struct ValidationAlgebra {
    // Every section in the document, for the direction-2 check above. A
    // reference into the caller's set: the algebra is constructed per
    // validate() call and outlived by it.
    const std::set<std::string>& sections;

    // What every roster in the document says about a name. Same
    // lifetime story as `sections`.
    const NameVisibility& visible;

    // A section: `s.children` are the fold's already-computed diagnostics
    // for each child (fold_with evaluates children before calling the
    // algebra), so this case only has to combine and re-label them.
    // Concatenate under the monoid, then prefix every result's `context`
    // with this section's own `stable_name` -- inherited context achieved
    // by post-processing child results on the way back up the fold
    // (decision expected-error-taxonomy). This post-processing step is why
    // the pure-monoid `fold_map` (vendor/tree_algorithms/.../fold_map.hpp)
    // is not the right verb here: fold_map has no hook to touch a child's
    // already-computed value before combining it with its siblings, and
    // that touch -- prefixing context -- is the entire point of this case.
    Diagnostics operator()(const ir::SectionF<Diagnostics>& s) const {
        Diagnostics combined = foundation::mconcat(s.children, diagnostics_monoid);
        if (s.stable_name.empty())
            return combined;
        return combined | std::views::transform([&](Diagnostic d) {
                   d.context = s.stable_name + "/" + d.context;
                   return d;
               }) |
               std::ranges::to<std::vector>();
    }

    // A synopsis: check that it has code at all, its one CodeText's span
    // table, the coverage invariant over the roster beside it, the same
    // roster for unmarked private data, the synopsis text itself for a
    // namespace qualifier the reader cannot follow, and every `Ref`
    // span in it for a stable name that names no section. A synopsis
    // ordinarily sits at the top level, outside every `\rSec`, so the section
    // prefixing below reaches it with nothing to add -- its own class name is
    // the only thing that says which class a member-level finding is about.
    Diagnostics operator()(const ir::Synopsis& v) const {
        const std::string context = v.name.empty() ? "synopsis" : v.name + "/synopsis";
        return diagnostics_monoid.combine(
            diagnostics_monoid.combine(
                diagnostics_monoid.combine(
                    diagnostics_monoid.combine(
                        diagnostics_monoid.combine(check_nonempty(context, v.code), check_spans(context, v.code)),
                        check_coverage(context, v.roster, sections)),
                    check_private_data(context, v.roster)),
                check_synopsis_leakage(context, v.code, visible)),
            check_dangling_ref(context, v.code, sections));
    }

    // A declared item: check every signature's span table (an item groups
    // overloads under one itemdecl, so more than one signature is
    // ordinary), every fragment of wording it carries -- the
    // signatures themselves plus each description element -- for leaked
    // names, an authored Constraints/Mandates against its derived
    // twin for drift, and an authored *Throws:* against what the
    // signatures' `noexcept` already says.
    Diagnostics operator()(const ir::SpecItem& v) const {
        Diagnostics decl_findings = foundation::mconcat_map(
            std::views::enumerate(v.decl.signatures),
            [this](const auto& entry) {
                const auto& [index, signature] = entry;
                const std::string context      = std::format("itemdecl[{}]", index);
                return diagnostics_monoid.combine(check_spans(context, signature),
                                                  check_leakage(context, signature, visible));
            },
            diagnostics_monoid);

        Diagnostics descr_findings = foundation::mconcat_map(
            v.descr.elements,
            [this](const ir::DescriptionElement& element) {
                return diagnostics_monoid.combine(
                    diagnostics_monoid.combine(check_table_structure(element), check_element_spans(element)),
                    check_element_leakage(element, visible));
            },
            diagnostics_monoid);

        Diagnostics drift_findings = check_mandates_constraints_drift(v.descr.elements);

        Diagnostics throws_findings = check_noexcept_throws(v);

        return diagnostics_monoid.combine(
            diagnostics_monoid.combine(diagnostics_monoid.combine(std::move(decl_findings), descr_findings),
                                       drift_findings),
            throws_findings);
    }

    // A free paragraph: prose, and so a leakage site like any other
    // wording -- class-general prose naming a private helper leaks exactly
    // as an *Equivalent to:* body does. Its `CodeInline`s also carry span
    // tables of their own, but no rule checks those yet; this case
    // reports leaks and no spans, rather
    // than silently reporting neither.
    Diagnostics operator()(const ir::FreeParagraph& v) const {
        return check_paragraph_leakage("paragraph", v.text, visible);
    }
};

// Fold @p node with the document's section names already in hand -- the
// shared core of both validate() entry points below, so neither can forget
// to supply them. Mirrors `latex.cpp`'s `render_layer`: the named visitor
// struct above carries the doc-commented cases, the lambda here is only the
// `std::visit` dispatch `fold_with` calls at each layer.
Diagnostics validate_with(const ir::Node& node, const std::set<std::string>& sections, const NameVisibility& visible) {
    const auto validation_layer = [&](const ir::NodeF<Diagnostics>& layer) {
        return std::visit(overloaded{ValidationAlgebra{sections, visible}}, layer);
    };
    return beman::tree_algorithms::fold_with<Diagnostics>(validation_layer, ir::node_fmap, ir::node_project, node);
}

} // namespace

Diagnostics validate(const ir::Node& node) {
    // A lone node is its own document as far as direction 2 is concerned:
    // collecting names from the node itself is what keeps this entry point
    // from reporting every routed section as dangling.
    return validate_with(node, section_names(node), name_visibility(node));
}

Diagnostics validate(const ir::Document& document) {
    // Collected across the whole forest first: a synopsis routinely sits at
    // the top level, before the `\rSec` that opens the section its members'
    // wording was routed to (design §3.2), so a per-root name set would call
    // every such routing dangling.
    const std::set<std::string> sections = foundation::mconcat_map(
        document.nodes, [](const ir::Node& node) { return section_names(node); }, names_monoid);
    // Likewise document-wide, and for the same reason one step further: the
    // wording that uses a class's members lives in `\rSec` roots the class's
    // own synopsis is not inside. The cost of that reach is that visibility
    // is a property of a *name*, not of a name in a class -- a member private
    // in one class and described in another reads as visible everywhere. That
    // is the conservative direction (a missed finding, not a false one), and
    // it is what a fragment carrying no record of which class it belongs to
    // permits.
    // The one part of visibility no node carries: a surviving
    // namespace qualifier is a fact the front end recorded about the header,
    // so it is seeded here rather than folded out of the forest. That is also
    // why validate(Node) below reports none of them -- a lone node is its own
    // document for section names and rosters, but it has no front end behind
    // it to have resolved a qualifier.
    NameVisibility visible = foundation::mconcat_map(
        document.nodes, [](const ir::Node& node) { return name_visibility(node); }, visibility_monoid);
    // Assigned, not merged: no node contributes to this field, so the fold
    // above always leaves it empty.
    visible.foreign =
        document.foreign_namespaces |
        std::views::transform([](const ir::ForeignNamespace& ns) { return std::pair{ns.name, ns.qualified}; }) |
        std::ranges::to<std::map<std::string, std::string>>();

    Diagnostics findings = foundation::mconcat_map(
        document.nodes,
        [&](const ir::Node& node) { return validate_with(node, sections, visible); },
        diagnostics_monoid);

    // The one rule that runs *outside* the fold: what it reports
    // is a name in a body no node holds, so there is no node to report it
    // against and no section path to prefix it with. It runs last for the
    // same reason it runs here -- the "only" clause it applies needs every
    // wording fragment in the document already collected (wording_names),
    // which is one more document-wide pre-pass, exactly like `sections` and
    // `visible` above.
    const std::set<std::string> wording = foundation::mconcat_map(
        document.nodes, [](const ir::Node& node) { return wording_names(node); }, names_monoid);
    return diagnostics_monoid.combine(std::move(findings),
                                      check_unextracted_uses(document.unextracted_uses, visible, wording));
}

bool has_errors(const Diagnostics& diagnostics) {
    return std::ranges::any_of(diagnostics, [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

std::string format_diagnostic(const Diagnostic& diagnostic) {
    // severity_label (diagnostic.hpp) is shared with the driver's
    // document_build printer, so the two cannot spell a severity
    // differently.
    return std::format("{}: {}: {}", diagnostic.context, severity_label(diagnostic.severity), diagnostic.message);
}

} // namespace beman::specgen::validate
