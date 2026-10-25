// src/beman/specgen/frontend/synopsis_extraction.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/synopsis_extraction.hpp>

#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/expos_spellings.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/merged_twins.hpp>
#include <beman/specgen/frontend/qualifiers.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/frontend/requires_clauses.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/section_markers.hpp>
#include <beman/specgen/span_sentinels.hpp>
#include <beman/specgen/synopsis_edits.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/TypeBase.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Specifiers.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

using grammar::parse_ref;
using span_sentinels::span_sentinel;
using span_sentinels::SpanInfo;
using synopsis_edits::add_dominant_edit;
using synopsis_edits::sort_back_to_front;
using synopsis_edits::SynopsisEdit;

namespace {

// Blank lines left directly before a class's closing brace -- by a member
// removed from the end of the body -- are dropped before formatting.
// clang-format drops them itself unless the closing `};` carries a trailing
// comment, which an exposition-only class's `// exposition only` is, so
// without this the marked class kept a blank line its unmarked twin does not.
std::string drop_blank_lines_before_close(std::string text) {
    const std::size_t close = text.rfind('}');
    if (close == std::string::npos)
        return text;
    const std::size_t content = text.find_last_not_of(" \t\n", close == 0 ? 0 : close - 1);
    if (content == std::string::npos)
        return text;
    const std::size_t first_newline = text.find('\n', content);
    const std::size_t last_newline  = text.rfind('\n', close);
    if (first_newline == std::string::npos || first_newline >= last_newline)
        return text;
    text.erase(first_newline, last_newline - first_newline);
    return text;
}

// The first offset of the run of cv-qualifier keywords written immediately
// before `begin`, or `begin` itself; never earlier than `floor`. A
// QualifiedTypeLoc begins at its unqualified child on this Clang -- the same
// quirk alias_rhs_source_range works around -- so the `const` in
// `const T& obj` sits outside the declared type's TypeLoc and would survive a
// mask taken from it (issue #33).
unsigned cv_run_begin(llvm::StringRef buffer, unsigned begin, unsigned floor) {
    static constexpr std::string_view qualifiers[] = {"const", "volatile"};
    // substrate generic algorithm: a backwards walk over a run of tokens
    // whose length is not known in advance, each step's start being the next
    // step's end -- no adaptor over the buffer says that.
    for (;;) {
        const std::size_t last = buffer.substr(0, begin).find_last_not_of(" \t\n\v\f\r");
        if (last == llvm::StringRef::npos)
            return begin;
        const auto written = [&](std::string_view keyword) {
            if (last + 1 < floor + keyword.size())
                return false;
            const std::size_t start = last + 1 - keyword.size();
            return std::string_view(buffer.substr(start, keyword.size())) == keyword &&
                   (start == 0 || !is_ident_char(buffer[start - 1]));
        };
        const auto* const found = std::ranges::find_if(qualifiers, written);
        if (found == std::ranges::end(qualifiers))
            return begin;
        begin = static_cast<unsigned>(last + 1 - found->size());
    }
}

// Drop `variable`'s initializer from one extraction, a copy-init's `=` with
// it. A dominant edit, so a qualifier drop or expos-use rewrite inside the
// initializer loses to it. Both the declared-type mask below and a static data
// member's bare `\seebelow` (issue #148) drop the initializer; only the former
// masks the type as well.
void add_initializer_drop(std::vector<SynopsisEdit>&  edits,
                          const clang::VarDecl*       variable,
                          llvm::StringRef             buffer,
                          unsigned                    decl_begin,
                          const clang::SourceManager& sm,
                          const clang::LangOptions&   lang_opts) {
    const clang::Expr* init = variable->getInit();
    if (init == nullptr || !init->getSourceRange().isValid())
        return;
    unsigned       init_begin = sm.getDecomposedLoc(init->getSourceRange().getBegin()).second;
    const unsigned init_end =
        sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(init->getSourceRange().getEnd(), 0, sm, lang_opts))
            .second;
    const std::size_t equal = llvm::StringRef(buffer.data(), init_begin).find_last_not_of(" \t\n\v\f\r");
    if (equal != llvm::StringRef::npos && buffer[equal] == '=')
        init_begin = static_cast<unsigned>(equal);
    if (init_begin >= decl_begin && init_end > init_begin)
        add_dominant_edit(edits, SynopsisEdit{init_begin, init_end, ""});
}

} // namespace

beman::specgen::ir::CodeText extract_synopsis(const clang::CXXRecordDecl*                      record,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              const std::set<const clang::Decl*>&              omit_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set,
                                              const SeeBelowMap&                               seebelow_map,
                                              const FreestandingMap&                           freestanding_map,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const clang::Decl*                               head_decl) {
    const clang::Decl*    head   = head_decl != nullptr ? head_decl : record;
    const llvm::StringRef buffer = file_buffer(sm, head->getBeginLoc());

    const unsigned              class_begin = sm.getDecomposedLoc(head->getBeginLoc()).second;
    const clang::SourceLocation end_of_brace =
        clang::Lexer::getLocForEndOfToken(record->getEndLoc(), 0, sm, lang_opts);
    const unsigned class_end = sm.getDecomposedLoc(end_of_brace).second;

    std::vector<SynopsisEdit>       edits;
    std::vector<SynopsisEdit>       seebelow_edits;
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;

    // Declarations that have no separate wording item carry their
    // library index in the synopsis spelling itself. The class name is a
    // global facility; eligible member names carry the enclosing class as the
    // span payload and render as \libmember. Use source locations before
    // formatting, like every other semantic synopsis rewrite.
    // An exposition-only class is not a library entity, so neither it nor its
    // members take an index entry; it is named by its exposid below.
    const auto expos_self        = expos_set.find(head->getCanonicalDecl());
    const auto add_library_index = [&](const clang::NamedDecl* named, std::string parent) {
        if (expos_self != expos_set.end())
            return;
        if (named == nullptr || named->getName().empty() || named->getLocation().isInvalid())
            return;
        const unsigned begin = sm.getDecomposedLoc(named->getLocation()).second;
        const unsigned end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(named->getLocation(), 0, sm, lang_opts)).second;
        if (begin < class_begin || end > class_end || end <= begin)
            return;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel] =
            SpanInfo{beman::specgen::ir::SpanKind::LibraryIndex, named->getNameAsString(), std::move(parent)};
        edits.push_back(SynopsisEdit{begin, end, sentinel});
    };

    // Rewrite `member_decl`'s declared name to an `\exposid` sentinel and append a
    // `// exposition only` comment after its `;`. Used for exposition-only data
    // members (and, later, expos helper functions).
    const auto add_exposid = [&](const clang::NamedDecl* member_decl, const std::string& name) {
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, name, name};
        const unsigned name_begin  = sm.getDecomposedLoc(member_decl->getLocation()).second;
        const unsigned name_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(member_decl->getLocation(), 0, sm, lang_opts))
                .second;
        edits.push_back(SynopsisEdit{name_begin, name_end, sentinel});
        unsigned semi_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(member_decl->getEndLoc(), 0, sm, lang_opts)).second;
        if (const std::optional<clang::Token> semi =
                clang::Lexer::findNextToken(member_decl->getEndLoc(), sm, lang_opts);
            semi && semi->is(clang::tok::semi))
            semi_end = sm.getDecomposedLoc(semi->getLocation()).second + 1;
        edits.push_back(SynopsisEdit{semi_end, semi_end, " // exposition only"});
    };

    // An exposition-only class is named by its exposid; an ordinary one
    // carries its library index. The marker is keyed on the declaration the
    // class arm extracts from: the ClassTemplateDecl for a primary template,
    // the record itself for a specialization. The `// exposition only` tail
    // goes in before formatting, so clang-format wraps the line it ends.
    if (expos_self != expos_set.end()) {
        const auto*       named    = llvm::cast<clang::NamedDecl>(head);
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel] = SpanInfo{beman::specgen::ir::SpanKind::ExposId, expos_self->second, expos_self->second};
        const unsigned name_begin = sm.getDecomposedLoc(named->getLocation()).second;
        const unsigned name_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(named->getLocation(), 0, sm, lang_opts)).second;
        edits.push_back(SynopsisEdit{name_begin, name_end, sentinel});
        const unsigned semi_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(record->getEndLoc(), 0, sm, lang_opts)).second;
        unsigned tail = semi_end;
        if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(record->getEndLoc(), sm, lang_opts);
            semi && semi->is(clang::tok::semi))
            tail = sm.getDecomposedLoc(semi->getLocation()).second + 1;
        edits.push_back(SynopsisEdit{tail, tail, " // exposition only"});
    } else {
        add_library_index(record, {});
    }

    // Start of the physical line containing `pos`, never earlier than
    // `class_begin` -- the reverse scan omit_line and the docblock-removal
    // pass below both need, written once with rfind rather than as two
    // hand-rolled decrementing whiles doing the same search.
    const auto line_start = [&buffer, class_begin](unsigned pos) -> unsigned {
        // `pos - class_begin` is unsigned: a `pos` before the class would wrap
        // to a huge length and search the whole rest of the buffer, returning a
        // line start *after* `pos`. Both call sites below are filtered to
        // `>= class_begin`, so this is unreachable today — but the two
        // decrementing loops this replaced could not underflow at all, and the
        // guard is what keeps that true of the replacement.
        if (pos <= class_begin)
            return class_begin;
        const std::size_t nl = buffer.substr(class_begin, pos - class_begin).rfind('\n');
        return nl == llvm::StringRef::npos ? class_begin : static_cast<unsigned>(class_begin + nl + 1);
    };

    // A class-scope `\ref` line labels the declarations up to the next such
    // line. Keep the line only if that run contributes something to the
    // synopsis. Collect the physical lines before walking members because
    // Clang can merge the header and a following `//! \merge` into one raw
    // comment. Comments lexically inside a direct declaration (a nested class
    // or an in-class function body) belong to that declaration, not this
    // class's groups, and remain untouched here.
    struct RefGroup {
        unsigned begin = 0;
        unsigned end   = 0;
        bool     live  = false;
    };
    std::vector<RefGroup>                         ref_groups;
    const std::map<unsigned, clang::RawComment*>* comments =
        file_comments(record->getASTContext(), sm, record->getBeginLoc());
    const auto inside_direct_declaration = [&](unsigned pos) {
        return std::ranges::any_of(record->decls(), [&](const clang::Decl* member) {
            if (member->isImplicit() || member->getBeginLoc().isInvalid() || member->getEndLoc().isInvalid())
                return false;
            const unsigned begin = sm.getDecomposedLoc(member->getBeginLoc()).second;
            const unsigned end =
                sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(member->getEndLoc(), 0, sm, lang_opts)).second;
            return begin < pos && pos < end;
        });
    };
    if (comments != nullptr) {
        // substrate generic algorithm: the outer traversal is a filtered fold
        // over Clang's ordered raw-comment map into ref_groups; the inner
        // traversal is a stateful physical-line scan because StringRef has no
        // line view that preserves byte offsets and the missing-final-newline
        // boundary needed for exact source edits.
        for (const auto& [offset, comment] : *comments | std::views::filter([&](const auto& entry) {
                 return entry.first >= class_begin && entry.first < class_end;
             })) {
            const llvm::StringRef raw           = comment->getRawText(sm);
            const unsigned        comment_begin = sm.getDecomposedLoc(comment->getBeginLoc()).second;
            // substrate generic algorithm: a stateful physical-line scan over
            // StringRef preserves each line's source offset and handles a final
            // line without a newline, which no available line view provides.
            for (std::size_t begin = 0; begin < raw.size();) {
                const std::size_t     newline  = raw.find('\n', begin);
                const std::size_t     end      = newline == llvm::StringRef::npos ? raw.size() : newline;
                const llvm::StringRef line     = raw.slice(begin, end);
                const unsigned        absolute = comment_begin + static_cast<unsigned>(begin);
                if (!inside_direct_declaration(absolute) &&
                    line_vocabulary(line.ltrim(" \t")) == CommentVocabulary::Draft &&
                    parse_ref(std::string_view(line.data(), line.size()))) {
                    unsigned absolute_end = comment_begin + static_cast<unsigned>(end);
                    if (newline != llvm::StringRef::npos)
                        ++absolute_end;
                    else {
                        if (absolute_end < class_end && buffer[absolute_end] == '\r')
                            ++absolute_end;
                        if (absolute_end < class_end && buffer[absolute_end] == '\n')
                            ++absolute_end;
                    }
                    ref_groups.push_back(RefGroup{line_start(absolute), absolute_end});
                }
                if (newline == llvm::StringRef::npos)
                    break;
                begin = newline + 1;
            }
        }
    }

    // Remove a member's whole physical line(s): from the start of its line (so
    // its indentation goes too) through the trailing `;` and the newline after
    // it, leaving no blank gap. `outer` is the decl whose text is removed — the
    // FriendDecl for a hidden friend, so `friend` is included.
    //
    // `removed_ranges` holds every span the synopsis will not contain: the
    // whole-member removals pushed here and the spliced-away in-class bodies
    // pushed in the member walk below. The qualifier/expos/comment edit
    // sources all filter against it — an edit nested inside a removal must
    // never be emitted, because the descending-offset apply loop applies the
    // inner (further-right) edit first and its watermark then skips the
    // *removal*. That inversion is exactly how a body naming a droppable
    // qualifier used to survive into the synopsis while its unqualified twin
    // was spliced to `;` (issue #5).
    std::vector<std::pair<unsigned, unsigned>> removed_ranges;
    const auto                                 omit_line = [&](const clang::Decl* outer) {
        const unsigned remove_begin = line_start(sm.getDecomposedLoc(outer->getBeginLoc()).second);
        unsigned       remove_end   = member_declaration_end(outer, sm, lang_opts);
        if (remove_end < class_end && buffer[remove_end] == '\r')
            ++remove_end;
        if (remove_end < class_end && buffer[remove_end] == '\n')
            ++remove_end;
        edits.push_back(SynopsisEdit{remove_begin, remove_end, ""});
        removed_ranges.emplace_back(remove_begin, remove_end);
    };

    // Access-specifier labels (design §6): a `private:`/`protected:` label whose
    // whole section is omitted is itself removed, so the synopsis carries no
    // empty exposition section. Tracked here, resolved after the member walk.
    // `anything_before` records whether any member had rendered when the
    // label was reached: in a struct or union, a `public:` label with nothing
    // rendered before it says nothing once the preceding private section is
    // elided — default member access is already public — and used to survive
    // as a stray leading label (issue #7).
    struct AccessLabel {
        const clang::Decl*     decl;
        clang::AccessSpecifier access;
        bool                   survivor        = false;
        bool                   anything_before = false;
    };
    std::vector<AccessLabel>   labels;
    bool                       member_shown  = false; // any member rendered so far, label-independent
    long                       current_label = -1;
    std::optional<std::size_t> current_group;
    std::size_t                next_group   = 0;
    const auto                 finish_group = [&] {
        if (current_group && !ref_groups[*current_group].live) {
            const RefGroup& group = ref_groups[*current_group];
            edits.push_back(SynopsisEdit{group.begin, group.end, ""});
        }
    };
    const auto open_groups_before = [&](unsigned offset) {
        // substrate generic algorithm: advance a persistent cursor through an
        // ordered vector, closing each prior group exactly once. A range query
        // would still need both the cursor mutation and finish_group side effect.
        while (next_group < ref_groups.size() && ref_groups[next_group].begin < offset) {
            finish_group();
            current_group = next_group++;
        }
    };
    const auto mark_survivor = [&] {
        member_shown = true;
        if (current_label >= 0)
            labels[static_cast<std::size_t>(current_label)].survivor = true;
        if (current_group)
            ref_groups[*current_group].live = true;
    };

    // Walk the members. Bodies of in-class function definitions (including hidden
    // friends) splice down to a bare `;`; `= default`/`= delete` members fall
    // through untouched (design §3.4). A member in `omit_set` (`\omit`/`\merge`)
    // is removed whole, and an unmarked *private* member is omitted (design §6:
    // private data/functions are exposition, not interface). An expos member
    // renders its name as an `\exposid` span; anything shown marks its
    // access label as surviving.
    //
    // substrate generic algorithm: a fold into `edits`/`labels`/`removed_ranges`
    // with several branches per member, walked in `record`'s own class-body
    // order -- the order the members were written in the header, not a
    // derived one. That order is a contract, not an accident: it is
    // what lets in-class members interleave correctly with their out-of-line
    // siblings later. Materializing this walk into a `views::filter` or
    // `views::transform` pipeline would still have to reproduce every branch
    // below verbatim inside the projection, trading the loop for the same
    // logic wrapped in more machinery.
    for (const clang::Decl* member : record->decls()) {
        if (!member->isImplicit() && member->getBeginLoc().isValid())
            open_groups_before(sm.getDecomposedLoc(member->getBeginLoc()).second);
        if (const auto* access = llvm::dyn_cast<clang::AccessSpecDecl>(member)) {
            labels.push_back(AccessLabel{member, access->getAccess(), false, member_shown});
            current_label = static_cast<long>(labels.size()) - 1;
            continue;
        }
        if (member->isImplicit())
            continue; // injected class name, implicit special members: no source text

        // A direct class-scope static_assert is a Mandate on instantiating the
        // class template, not part of the library synopsis (design
        // §5.2). derive_class_mandates turns every such declaration into the
        // adjacent general-subclause paragraph; remove it here regardless of
        // access, since StaticAssertDecl itself commonly reports AS_none.
        if (llvm::isa<clang::StaticAssertDecl>(member)) {
            omit_line(member);
            continue;
        }

        // Every suppressing marker applies to every authored member kind,
        // not just functions in build_omit_set. In particular, `\elsewhere`
        // may promise that a paper hand-authors a data member or alias while
        // this generated synopsis leaves it out (issue #113).
        if (suppresses_generated_entity(docblock_directives(member, sm))) {
            omit_line(member);
            continue;
        }

        // The source fields of an anonymous struct/union belong to its nested
        // CXXRecordDecl; the direct FieldDecl/IndirectFieldDecl projections are
        // implicit and were skipped above. Keep the wrapper when any real
        // descendant is exposition-only, rewrite those descendants in place,
        // and do not expose unmarked storage through a private wrapper.
        if (const auto* nested = llvm::dyn_cast<clang::CXXRecordDecl>(member);
            nested != nullptr && nested->isAnonymousStructOrUnion()) {
            const std::vector<RealRecordMember> descendants =
                real_record_members(nested, member->getAccess() == clang::AS_private);
            const bool has_expos = std::ranges::any_of(descendants, [&](const RealRecordMember& descendant) {
                const auto* named = llvm::dyn_cast<clang::NamedDecl>(descendant.decl);
                return named != nullptr && expos_set.contains(named->getCanonicalDecl());
            });
            if (has_expos) {
                // substrate generic algorithm: a conditional edit fold over
                // the already flattened descendants, preserving source order.
                for (const RealRecordMember& descendant : descendants) {
                    const auto* named = llvm::dyn_cast<clang::NamedDecl>(descendant.decl);
                    if (named != nullptr) {
                        if (const auto it = expos_set.find(named->getCanonicalDecl()); it != expos_set.end()) {
                            add_exposid(named, it->second);
                            continue;
                        }
                    }
                    if (descendant.effectively_private)
                        omit_line(descendant.decl);
                }
                mark_survivor();
                continue;
            }
        }

        // Private, unmarked members are exposition, not interface: drop them
        // (design §6). A member function template is represented by its
        // FunctionTemplateDecl wrapper here, while expos_set is keyed to the
        // underlying FunctionDecl's canonical declaration.
        const clang::FunctionDecl* member_fn = member_function_or_template(member);
        const clang::Decl*         expos_key =
            member_fn != nullptr ? member_fn->getCanonicalDecl() : member->getCanonicalDecl();
        if (member->getAccess() == clang::AS_private && expos_set.find(expos_key) == expos_set.end()) {
            omit_line(member);
            continue;
        }

        // An exposition-only nested class (issue #80).  Marked `\expos` alone
        // it renders under its exposition name, definition and all; marked
        // bare `\seebelow` as well it renders as a declaration, which is the
        // shape [range.transform.view] prints -- the class is specified in its
        // own subclause, and its state is not the synopsis's business.
        const clang::CXXRecordDecl* nested_record = llvm::dyn_cast<clang::CXXRecordDecl>(member);
        if (const auto* nested_tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(member))
            nested_record = nested_tmpl->getTemplatedDecl();
        if (nested_record != nullptr && !nested_record->isAnonymousStructOrUnion() &&
            nested_record->isThisDeclarationADefinition()) {
            if (const auto it = expos_set.find(expos_key); it != expos_set.end()) {
                const auto* named = llvm::cast<clang::NamedDecl>(member);
                if (docblock_directives(member, sm).seebelow) {
                    const unsigned        record_begin = sm.getDecomposedLoc(nested_record->getBeginLoc()).second;
                    clang::SourceLocation end_loc =
                        clang::Lexer::getLocForEndOfToken(member->getEndLoc(), 0, sm, lang_opts);
                    if (const std::optional<clang::Token> semi =
                            clang::Lexer::findNextToken(member->getEndLoc(), sm, lang_opts);
                        semi && semi->is(clang::tok::semi))
                        end_loc = clang::Lexer::getLocForEndOfToken(semi->getLocation(), 0, sm, lang_opts);
                    const unsigned    record_end = sm.getDecomposedLoc(end_loc).second;
                    const std::string sentinel   = span_sentinel(span_n++);
                    sentinels[sentinel]          = SpanInfo{ir::SpanKind::ExposId, it->second, it->second};
                    const llvm::StringRef tag    = nested_record->getKindName();
                    edits.push_back(
                        SynopsisEdit{record_begin,
                                     record_end,
                                     std::string(tag.data(), tag.size()) + " " + sentinel + "; // exposition only"});
                    // Nothing is edited inside a replaced range (design §3.4):
                    // an inner edit is applied first and its overlap watermark
                    // would then suppress this one, leaving the definition in
                    // the synopsis.
                    removed_ranges.emplace_back(record_begin, record_end);
                } else {
                    add_exposid(named, it->second);
                }
                mark_survivor();
                continue;
            }
        }

        if (const auto* field = llvm::dyn_cast<clang::FieldDecl>(member)) {
            if (const auto it = expos_set.find(field->getCanonicalDecl()); it != expos_set.end())
                add_exposid(field, it->second);
            mark_survivor(); // a shown (public or expos) data member
            continue;
        }

        // An `\expos` static data member, or static data member template,
        // renders like exposition-only data (issue #172). A template's name
        // and `;` are its templated declaration's.
        if (llvm::isa<clang::VarDecl, clang::VarTemplateDecl>(member)) {
            if (const auto it = expos_set.find(member->getCanonicalDecl()); it != expos_set.end()) {
                const clang::NamedDecl* named = llvm::cast<clang::NamedDecl>(member);
                if (const auto* var_tmpl = llvm::dyn_cast<clang::VarTemplateDecl>(member))
                    named = var_tmpl->getTemplatedDecl();
                add_exposid(named, it->second);
                mark_survivor();
                continue;
            }
        }

        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(member)) {
            // A documented alias gets a standalone member index on its
            // itemdecl. An unmarked alias is synopsis-only, so index its name
            // here instead. Exposition names already occupy this byte range
            // with an ExposId span and cannot be nested in CodeText.
            if (!has_docblock(alias, sm) && !expos_set.contains(alias->getCanonicalDecl()))
                add_library_index(alias, record->getNameAsString());
            // An `\expos` alias renders like exposition-only data: exposid
            // name, `// exposition only` tail, and its uses elsewhere in the
            // class rewritten by the expos-use pass — the fixit the private
            // `using type = raw;` leakage error offers (issue #7).
            if (const auto it = expos_set.find(alias->getCanonicalDecl()); it != expos_set.end())
                add_exposid(alias, it->second);
            if (const auto mask = alias_mask(docblock_directives(alias, sm))) {
                if (const auto range = alias_rhs_source_range(alias, sm, lang_opts)) {
                    const unsigned begin = sm.getDecomposedLoc(range->getBegin()).second;
                    const unsigned end =
                        sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range->getEnd(), 0, sm, lang_opts))
                            .second;
                    if (end > begin) {
                        const std::string  sentinel = span_sentinel(span_n++);
                        const ir::SpanKind kind =
                            *mask == AliasMask::ImplDefined ? ir::SpanKind::ImplDefined : ir::SpanKind::SeeBelow;
                        const std::string display =
                            *mask == AliasMask::ImplDefined ? "implementation-defined" : "SEEBELOW";
                        sentinels[sentinel] = SpanInfo{kind, display, ""};
                        seebelow_edits.push_back(SynopsisEdit{begin, end, sentinel});
                    }
                }
            }
            mark_survivor();
            continue;
        }

        const clang::FunctionDecl* fn = member_fn;
        if (fn == nullptr) {
            mark_survivor(); // a shown non-function member (type alias, nested type, ...)
            continue;
        }

        if (omit_set.count(fn->getCanonicalDecl()) != 0) {
            omit_line(member); // \omit / \merge
            continue;
        }

        if (const auto it = expos_set.find(fn->getCanonicalDecl()); it != expos_set.end())
            add_exposid(fn, it->second);
        mark_survivor();

        // A survivor of `\merge`d twins says only what the pair shares; the
        // cuts are dominant like the see-below edits, and applied with them.
        seebelow_edits.append_range(twin_cut_edits(fn, sm, lang_opts));

        // Bare \seebelow replaces the return type. The named
        // targets replace only the operand of noexcept(...) / explicit(...),
        // leaving the keyword and parentheses in authored source.
        if (const auto marked = seebelow_map.find(fn->getCanonicalDecl()); marked != seebelow_map.end()) {
            if (const auto range = seebelow_source_range(fn, marked->second)) {
                const unsigned begin = sm.getDecomposedLoc(range->getBegin()).second;
                const unsigned end =
                    sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range->getEnd(), 0, sm, lang_opts)).second;
                if (end > begin) {
                    const std::string sentinel = span_sentinel(span_n++);
                    sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::SeeBelow, "SEEBELOW", ""};
                    seebelow_edits.push_back(SynopsisEdit{begin, end, sentinel});
                }
            }
        }

        const auto freestanding         = freestanding_map.find(fn->getCanonicalDecl());
        const auto freestanding_comment = [&]() -> std::string_view {
            if (freestanding == freestanding_map.end())
                return {};
            return freestanding->second == FreestandingKind::Deleted ? " // freestanding-deleted" : " // freestanding";
        }();

        // Clang may synthesize a CompoundStmt for an explicitly defaulted
        // special member once it is ODR-used. That is not authored source:
        // its range can be the final character of `default`, and splicing it
        // would turn `= default;` into `= defaul;;`.
        if (fn->isDefaulted() || fn->isDeleted() || !fn->doesThisDeclarationHaveABody()) {
            if (!freestanding_comment.empty()) {
                const unsigned semi_end = member_declaration_end(fn, sm, lang_opts);
                edits.push_back(SynopsisEdit{semi_end, semi_end, std::string(freestanding_comment)});
            }
            continue;
        }
        const clang::Stmt* body = fn->getBody();
        if (body == nullptr)
            continue;

        const unsigned              body_begin = spliced_tail_begin(fn, body, sm);
        const clang::SourceLocation body_end_tok =
            clang::Lexer::getLocForEndOfToken(body->getEndLoc(), 0, sm, lang_opts);
        const unsigned body_end = sm.getDecomposedLoc(body_end_tok).second;
        edits.push_back(SynopsisEdit{body_begin, body_end, std::format(";{}", freestanding_comment)});
        removed_ranges.emplace_back(body_begin, body_end);
    }
    open_groups_before(class_end);
    finish_group();

    // Drop `private:`/`protected:` labels whose entire section was omitted —
    // and, in a struct or union, a `public:` label with nothing rendered
    // before it: default member access is already public there, so once the
    // preceding private section is elided the label says nothing (issue #7).
    // In a class the leading `public:` stays, as every draft class synopsis
    // writes it.
    const auto orphaned_label = [&record](const AccessLabel& label) {
        if (!label.survivor && (label.access == clang::AS_private || label.access == clang::AS_protected))
            return true;
        return label.access == clang::AS_public && !record->isClass() && !label.anything_before;
    };
    // substrate generic algorithm: the filtering is real (views::filter,
    // above), but what remains is a call into the same omit_line fold the
    // member walk above feeds -- not a build-a-container step, just
    // triggering a side effect per matching element. `std::ranges::for_each`
    // would swap the `for` keyword for a call and change nothing else, and
    // no other tree file reaches for it for exactly that reason.
    for (const AccessLabel& label : labels | std::views::filter(orphaned_label))
        omit_line(label.decl);

    // Namespace qualifiers (design §3.5): drop `std::` and the header's own namespace
    // wherever they were written inside the class. A qualifier inside a member
    // that was removed wholesale — or inside a spliced-away in-class body —
    // is already gone with it; emitting it too would be an edit nested inside
    // another, which suppresses the removal instead (see removed_ranges).
    const auto inside_removed = [&removed_ranges](unsigned begin, unsigned end) {
        return std::ranges::any_of(removed_ranges, [&](const auto& r) { return begin >= r.first && end <= r.second; });
    };
    // The record's own rewrites, plus the template head's: two roots, because
    // no single AST node spans both without spanning the record twice.
    std::vector<std::pair<unsigned, unsigned>> qualifier_edits =
        namespace_qualifier_edits(const_cast<clang::CXXRecordDecl*>(record), ns_drop_set, sm, lang_opts, expos_set);
    std::vector<ExposUse> uses = expos_uses(const_cast<clang::CXXRecordDecl*>(record), expos_set, sm, lang_opts);
    if (const clang::TemplateParameterList* head_params = head_parameters(head_decl)) {
        const auto gather = [&](auto* node) {
            qualifier_edits.append_range(namespace_qualifier_edits(node, ns_drop_set, sm, lang_opts, expos_set));
            uses.append_range(expos_uses(node, expos_set, sm, lang_opts));
        };
        // substrate generic algorithm: a for_each-shaped walk driving the
        // gather's side effect over two unlike roots -- the parameters, then
        // the clause -- not a transform building a container.
        for (const clang::NamedDecl* param : *head_params)
            gather(const_cast<clang::NamedDecl*>(param));
        if (const clang::Expr* clause = head_params->getRequiresClause())
            gather(const_cast<clang::Expr*>(clause));
    }

    edits.append_range(qualifier_edits | std::views::filter([&](const auto& p) {
                           return p.first >= class_begin && p.second <= class_end &&
                                  !inside_removed(p.first, p.second);
                       }) |
                       std::views::transform([](const auto& p) { return SynopsisEdit{p.first, p.second, ""}; }));

    // substrate generic algorithm: each use allocates a unique sentinel while
    // conditionally emitting its qualifier deletion, so this is a stateful
    // flat-map into two coupled outputs rather than a transform.
    for (const ExposUse& use : uses) {
        if (use.name_begin < class_begin || use.name_end > class_end || inside_removed(use.name_begin, use.name_end))
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }

    // substrate generic algorithm: each dominant edit erases any nested edits
    // already accumulated, so applying the sequence mutates the same target
    // collection that it consumes conceptually rather than transforming it.
    for (SynopsisEdit& edit : seebelow_edits)
        add_dominant_edit(edits, std::move(edit));

    // Docblocks: any raw comment inside the class span carrying specgen
    // markup (`//!`, `/*!`) or Doxygen (`///`, `/** */`) is removed outright;
    // draft-form comments (plain `//`, e.g. `\ref{...}` group headers) are
    // left in place verbatim. The two Doxygen spellings are not markup, but
    // the draft does not print implementation documentation either, so they
    // are cut here rather than kept.
    if (comments != nullptr) {
        // substrate generic algorithm: the offset-bounds check is filtered
        // above, but what a surviving comment turns into
        // (stripped_comment_start's scan, the line-start recomputation, the
        // trailing \r\n absorption)
        // is a conditional, multi-step computation of one SynopsisEdit --
        // a filter-map with no std::ranges name (there is no
        // views::filter_map), and the optional-then-filter-then-transform
        // pattern that would fake one is harder to read than this loop.
        for (const auto& [offset, comment] : *comments | std::views::filter([&](const auto& kv) {
                 return kv.first >= class_begin && kv.first < class_end && !inside_removed(kv.first, kv.first);
             })) {
            const llvm::StringRef            raw_text    = comment->getRawText(sm);
            const std::optional<std::size_t> markup_from = stripped_comment_start(raw_text);
            if (!markup_from)
                continue;
            // Strip from the first non-draft line on, so a `\ref` group header
            // merged into the same RawComment survives in the synopsis.
            unsigned comment_begin =
                sm.getDecomposedLoc(comment->getSourceRange().getBegin()).second + static_cast<unsigned>(*markup_from);
            const clang::SourceLocation comment_end_tok =
                clang::Lexer::getLocForEndOfToken(comment->getSourceRange().getEnd(), 0, sm, lang_opts);
            unsigned comment_end = sm.getDecomposedLoc(comment_end_tok).second;
            // Take the whole line(s): the indentation before the markup and the
            // newline after it, so a stripped docblock leaves no blank gap
            // between the members it sat among.
            const unsigned line_begin = line_start(comment_begin);
            if (buffer.substr(line_begin, comment_begin - line_begin).find_first_not_of(" \t") ==
                llvm::StringRef::npos)
                comment_begin = line_begin;
            if (comment_end < class_end && buffer[comment_end] == '\r')
                ++comment_end;
            if (comment_end < class_end && buffer[comment_end] == '\n')
                ++comment_end;
            edits.push_back(SynopsisEdit{comment_begin, comment_end, ""});
        }
    }

    std::string text = buffer.substr(class_begin, class_end - class_begin).str();
    text += ';'; // the token after `}`, not part of [class_begin, class_end).

    // Apply edits back to front (by descending begin) so an earlier edit's
    // offsets are never invalidated by a later one applied first. Overlapping
    // edits are skipped rather than applied: a whole-line removal (an omitted
    // member) subsumes any qualifier/expos edit inside it, and applying both
    // would corrupt the text.
    sort_back_to_front(edits);
    unsigned applied_begin = class_end + 1; // lowest offset touched so far
    // substrate generic algorithm: a mutation whose correctness depends on
    // descending position is a scatter, not a fold in disguise. The
    // `applied_begin` watermark above (the lowest offset touched so far)
    // skips an edit whose end reaches into territory a further-right edit
    // already consumed.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue; // overlaps an already-applied (further-right) edit
        const std::size_t rel_begin = edit.begin - class_begin;
        const std::size_t rel_end   = edit.end - class_begin;
        text.replace(rel_begin, rel_end - rel_begin, edit.replacement);
        applied_begin = edit.begin;
    }

    // The surviving `\ref{...}` group headers become Ref sentinels.
    // Run here, on the post-edit text, rather than as SynopsisEdits: an edit
    // landing inside a docblock that a whole-line removal also covers would
    // trip the overlap watermark above and suppress the *removal*, leaving the
    // docblock in the synopsis. Scanning after the edits means only text that
    // actually survives is scanned, which is also the only text a span can
    // legitimately point into.
    text = apply_ref_sentinels(std::move(text), span_n, sentinels);

    // Normalization steps 2–3 (design §3.6): reformat with the draft FormatStyle,
    // then recover any spans from the sentinels spliced in above. Formatting runs
    // *before* span recovery — span offsets are only ever computed against
    // already-formatted text, never the other way around.
    const llvm::StringRef record_tag = record->getKindName();
    return format_and_recover(drop_blank_lines_before_close(std::move(text)),
                              sentinels,
                              std::string_view(record_tag.data(), record_tag.size()),
                              kNamespaceIndent);
}

void add_declaration_masks(std::vector<SynopsisEdit>&        edits,
                           std::map<std::string, SpanInfo>&  sentinels,
                           unsigned&                         span_n,
                           const clang::VarDecl*             unspecified,
                           std::optional<clang::SourceRange> see_below,
                           llvm::StringRef                   buffer,
                           unsigned                          decl_begin,
                           unsigned                          decl_end,
                           const clang::SourceManager&       sm,
                           const clang::LangOptions&         lang_opts) {
    if (unspecified != nullptr) {
        // The declared type, masked whole: a dominant edit, so a qualifier
        // drop or expos-use rewrite inside the spelling loses to the mask
        // instead of tripping the watermark (the alias RHS rule). Whole means
        // every token from the type's first through to the declared name,
        // which the TypeLoc bounds at neither end: a leading cv-qualifier is
        // outside it (cv_run_begin), and so is the space a declarator
        // operator eats. Masking the TypeLoc alone left the `const` of
        // `const T &obj` standing against the placeholder and lost the space
        // before the name (issue #33).
        const clang::TypeSourceInfo* tsi = unspecified->getTypeSourceInfo();
        const clang::SourceRange range   = tsi != nullptr ? tsi->getTypeLoc().getSourceRange() : clang::SourceRange{};
        if (range.isValid()) {
            const unsigned name_begin = sm.getDecomposedLoc(unspecified->getLocation()).second;
            const unsigned type_begin = cv_run_begin(buffer, sm.getDecomposedLoc(range.getBegin()).second, decl_begin);
            if (type_begin >= decl_begin && name_begin > type_begin) {
                const std::string sentinel = span_sentinel(span_n++);
                sentinels[sentinel] =
                    SpanInfo{beman::specgen::ir::SpanKind::Placeholder, "unspecified", "unspecified"};
                // The separating space is the mask's own: the declarator
                // operator that used to carry it is inside the span now.
                add_dominant_edit(edits, SynopsisEdit{type_begin, name_begin, sentinel + " "});
            }
        }
        // The initializer is implementation all the way down; the draft
        // writes the declaration alone. (see_below below is the same idea for
        // the kinds whose definition, rather than whose declared type, is the
        // implementation.)
        add_initializer_drop(edits, unspecified, buffer, decl_begin, sm, lang_opts);
    }

    // The definition, masked whole: an alias's RHS or a concept's
    // constraint-expression (issue #50). A dominant edit for the same reason
    // the declared-type mask is one -- a qualifier drop or expos-use rewrite
    // inside the masked spelling loses to the mask rather than tripping the
    // watermark.
    if (see_below && see_below->isValid()) {
        const unsigned begin = sm.getDecomposedLoc(see_below->getBegin()).second;
        const unsigned end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(see_below->getEnd(), 0, sm, lang_opts)).second;
        if (begin >= decl_begin && end > begin && end <= decl_end) {
            const std::string sentinel = span_sentinel(span_n++);
            sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::SeeBelow, "SEEBELOW", ""};
            add_dominant_edit(edits, SynopsisEdit{begin, end, sentinel});
        }
    }
}

beman::specgen::ir::CodeText
extract_freestanding_declaration(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                 bool                                             exposition,
                                 std::optional<std::string_view>                  record_tag,
                                 const clang::VarDecl*                            unspecified,
                                 std::optional<clang::SourceRange>                see_below,
                                 const clang::VarDecl*                            uninitialized) {
    const clang::SourceLocation begin_loc  = named->getBeginLoc();
    const unsigned              decl_begin = sm.getDecomposedLoc(begin_loc).second;

    clang::SourceLocation end_loc = clang::Lexer::getLocForEndOfToken(named->getEndLoc(), 0, sm, lang_opts);
    if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(named->getEndLoc(), sm, lang_opts);
        semi && semi->is(clang::tok::semi))
        end_loc = clang::Lexer::getLocForEndOfToken(semi->getLocation(), 0, sm, lang_opts);
    const unsigned decl_end = sm.getDecomposedLoc(end_loc).second;

    const llvm::StringRef buffer = file_buffer(sm, begin_loc);
    std::string           text   = buffer.substr(decl_begin, decl_end - decl_begin).str();
    if (exposition)
        text += " // exposition only";

    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       edits;
    unsigned                        span_n = 0;

    const auto marked = expos_set.find(named->getCanonicalDecl());
    if (exposition && marked != expos_set.end()) {
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, marked->second, marked->second};
        // The declared name's own token. A TypeAliasTemplateDecl's
        // getLocation() is the `using` keyword on this Clang, so the rename
        // reads the templated declaration's location instead — which is the
        // name token for every templated kind.
        clang::SourceLocation name_loc = named->getLocation();
        if (const auto* tmpl = llvm::dyn_cast<clang::TemplateDecl>(named);
            tmpl != nullptr && tmpl->getTemplatedDecl() != nullptr)
            name_loc = tmpl->getTemplatedDecl()->getLocation();
        const unsigned name_begin = sm.getDecomposedLoc(name_loc).second;
        const unsigned name_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(name_loc, 0, sm, lang_opts)).second;
        edits.push_back(SynopsisEdit{name_begin, name_end, sentinel});
    }

    edits.append_range(
        namespace_qualifier_edits(const_cast<clang::NamedDecl*>(named), ns_drop_set, sm, lang_opts, expos_set) |
        std::views::filter([&](const auto& range) { return range.first >= decl_begin && range.second <= decl_end; }) |
        std::views::transform([](const auto& range) { return SynopsisEdit{range.first, range.second, ""}; }));
    // substrate generic algorithm: the declaration's resolved uses allocate
    // sentinels while scattering name and optional qualifier edits.
    for (const ExposUse& use : expos_uses(const_cast<clang::NamedDecl*>(named), expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin || use.name_end > decl_end)
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }

    add_declaration_masks(
        edits, sentinels, span_n, unspecified, see_below, buffer, decl_begin, decl_end, sm, lang_opts);
    if (uninitialized != nullptr)
        add_initializer_drop(edits, uninitialized, buffer, decl_begin, sm, lang_opts);

    sort_back_to_front(edits);
    unsigned applied_begin = decl_end + 1;
    // substrate generic algorithm: the same descending-offset scatter and
    // overlap watermark as class synopsis and itemdecl extraction.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        text.replace(edit.begin - decl_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    // An exposition-only declaration is a standalone synopsis, indented inside
    // `namespace std {` once formatted; every other caller's is an itemdecl.
    return format_and_recover(std::move(text), sentinels, record_tag, exposition ? kNamespaceIndent : 0);
}

beman::specgen::ir::CodeText
extract_namespace_expos_synopsis(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                 const clang::VarDecl*                            unspecified,
                                 std::optional<clang::SourceRange>                see_below) {
    // An exposition-only class template (issue #23) keeps the draft's
    // template-head line break, the same FormatStyle nudge every record
    // extraction passes; the non-record kinds format as before.
    std::optional<std::string_view> record_tag;
    const clang::CXXRecordDecl*     record = llvm::dyn_cast<clang::CXXRecordDecl>(named);
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(named))
        record = tmpl->getTemplatedDecl();
    if (record != nullptr) {
        const llvm::StringRef tag = record->getKindName();
        record_tag                = std::string_view(tag.data(), tag.size());
    }
    return extract_freestanding_declaration(
        named, sm, lang_opts, ns_drop_set, expos_set, /*exposition=*/true, record_tag, unspecified, see_below);
}

} // namespace beman::specgen::frontend
