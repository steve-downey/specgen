// src/beman/specgen/split_inline/rewrite.cpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/split_inline/rewrite.hpp>

#include <beman/specgen/frontend/docblock_lookup.hpp>
#include <beman/specgen/frontend/record_members.hpp>
#include <beman/specgen/rsec.hpp>
#include <beman/specgen/section_markers.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::split_inline {

namespace {

namespace frontend = beman::specgen::frontend;
namespace grammar  = beman::specgen::grammar;

constexpr std::string_view kWhitespace = " \t\r\n\v\f";

// ---------------------------------------------------------------------------
// The main file as text and tokens
// ---------------------------------------------------------------------------

struct Tok {
    unsigned    begin         = 0;
    unsigned    end           = 0;
    bool        at_line_start = false;
    std::string spelling;
};

// The main file's buffer, its raw token table, and the offset arithmetic
// every later step does over it. Offsets are byte offsets into the main
// file; a location in another file (a macro expansion, an included file) is
// first mapped to its file location, so a token spelled through a macro
// reads at its invocation.
class Source {
  public:
    Source(const clang::SourceManager& sm, const clang::LangOptions& lo)
        : sm_(sm), lo_(lo), main_(sm.getMainFileID()), buffer_(sm.getBufferData(main_)), tokens_(lex_all()) {}

    const clang::SourceManager& sm() const { return sm_; }
    const clang::LangOptions&   lang_opts() const { return lo_; }
    clang::FileID               main() const { return main_; }
    llvm::StringRef             buffer() const { return buffer_; }
    unsigned                    size() const { return static_cast<unsigned>(buffer_.size()); }

    bool     in_main(clang::SourceLocation loc) const { return sm_.getFileID(sm_.getFileLoc(loc)) == main_; }
    unsigned offset(clang::SourceLocation loc) const { return sm_.getFileOffset(sm_.getFileLoc(loc)); }
    unsigned end_of(clang::SourceLocation loc) const {
        return offset(clang::Lexer::getLocForEndOfToken(sm_.getFileLoc(loc), 0, sm_, lo_));
    }
    std::string text(unsigned begin, unsigned end) const { return std::string(buffer_.substr(begin, end - begin)); }
    unsigned    line_of(unsigned off) const { return sm_.getLineNumber(main_, off); }

    // The offset of the first character of the line holding `off`.
    unsigned line_start(unsigned off) const {
        const std::size_t nl = buffer_.substr(0, off).rfind('\n');
        return nl == llvm::StringRef::npos ? 0 : static_cast<unsigned>(nl + 1);
    }
    // The offset just past the newline ending the line holding `off`, or the
    // end of the buffer.
    unsigned next_line_start(unsigned off) const {
        const std::size_t nl = buffer_.find('\n', off);
        return nl == llvm::StringRef::npos ? size() : static_cast<unsigned>(nl + 1);
    }
    bool blank_between(unsigned begin, unsigned end) const {
        return begin >= end ||
               buffer_.substr(begin, end - begin).find_first_not_of(kWhitespace) == llvm::StringRef::npos;
    }
    // Back over whitespace from `off`: the offset just past the last
    // non-blank character before it.
    unsigned trim_back(unsigned off) const {
        const std::size_t last = buffer_.substr(0, off).find_last_not_of(kWhitespace);
        return last == llvm::StringRef::npos ? 0 : static_cast<unsigned>(last + 1);
    }

    // The tokens whose begin lies in [begin, end).
    std::span<const Tok> tokens_in(unsigned begin, unsigned end) const {
        const auto first = std::ranges::lower_bound(tokens_, begin, {}, &Tok::begin);
        const auto last  = std::ranges::lower_bound(tokens_, end, {}, &Tok::begin);
        return {first, last};
    }

  private:
    std::vector<Tok> lex_all() const {
        std::vector<Tok> tokens;
        clang::Lexer     lexer(main_, sm_.getBufferOrFake(main_), sm_, lo_);
        clang::Token     tok;
        // substrate generic algorithm: the raw lexer is a pull source with
        // no range adaptor; this is the unfold that fills the token table.
        while (!lexer.LexFromRawLexer(tok)) {
            const unsigned begin = sm_.getFileOffset(tok.getLocation());
            tokens.push_back({begin,
                              begin + tok.getLength(),
                              tok.isAtStartOfLine(),
                              std::string(buffer_.substr(begin, tok.getLength()))});
        }
        return tokens;
    }

    const clang::SourceManager& sm_;
    const clang::LangOptions&   lo_;
    clang::FileID               main_;
    llvm::StringRef             buffer_;
    std::vector<Tok>            tokens_;
};

// ---------------------------------------------------------------------------
// Conditional-compilation regions
// ---------------------------------------------------------------------------

struct Region {
    unsigned begin = 0;
    unsigned end   = 0;
};

bool opens_region(std::string_view directive) {
    return directive == "if" || directive == "ifdef" || directive == "ifndef";
}

// Every `#if`...`#endif` pair of the main file, nested ones included, as the
// byte range from the opening directive's `#` to the end of the `#endif`
// line. A member whose definition lies inside one of these, when the class
// as a whole does not, is refused rather than moved out from under its
// guard.
std::vector<Region> conditional_regions(const Source& src) {
    std::vector<Region>   regions;
    std::vector<unsigned> open;
    const auto            toks = src.tokens_in(0, src.size());
    // substrate generic algorithm: a bracket matcher over directive tokens,
    // whose state is the stack of open `#if`s.
    for (auto it = toks.begin(); it != toks.end(); ++it) {
        if (!(it->at_line_start && it->spelling == "#") || std::next(it) == toks.end())
            continue;
        const std::string& directive = std::next(it)->spelling;
        if (opens_region(directive)) {
            open.push_back(it->begin);
        } else if (directive == "endif" && !open.empty()) {
            regions.push_back({open.back(), src.next_line_start(it->begin)});
            open.pop_back();
        }
    }
    return regions;
}

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

std::string trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos)
        return {};
    const std::size_t last = s.find_last_not_of(kWhitespace);
    return std::string(s.substr(first, last - first + 1));
}

std::vector<std::string_view> lines_of(std::string_view text) {
    return std::views::split(text, '\n') |
           std::views::transform([](auto&& part) { return std::string_view(part.begin(), part.end()); }) |
           std::ranges::to<std::vector>();
}

std::string join_lines(const std::vector<std::string>& lines) {
    return std::views::join_with(lines, '\n') | std::ranges::to<std::string>();
}

// `text` with `indent` removed from the front of every line that starts with
// it: the in-class indentation of a body and its comment, which are moving to
// namespace scope.
std::string dedent(std::string_view text, std::string_view indent) {
    if (indent.empty())
        return std::string(text);
    const auto strip = [&](std::string_view line) {
        return std::string(line.starts_with(indent) ? line.substr(indent.size()) : line);
    };
    return join_lines(lines_of(text) | std::views::transform(strip) | std::ranges::to<std::vector>());
}

bool starts_markup(std::string_view line) {
    const std::string t = trim(line);
    return t.starts_with("//!") || t.starts_with("/*!");
}

// A `//!` line holding nothing but one of the markers §3.3 allows on the
// in-class declaration. A block made only of these stays where the author
// put it.
bool is_declaration_marker_line(std::string_view line) {
    std::string t = trim(line);
    if (t.starts_with("//!"))
        t = trim(t.substr(3));
    if (t.empty())
        return true;
    constexpr std::array markers{
        std::string_view("\\omit"),
        std::string_view("\\merge"),
        std::string_view("\\elsewhere"),
        std::string_view("\\expos"),
        std::string_view("\\seebelow"),
        std::string_view("\\freestanding"),
    };
    const auto is_marker = [&](std::string_view marker) {
        if (!t.starts_with(marker))
            return false;
        const std::string_view rest = std::string_view(t).substr(marker.size());
        return rest.empty() || rest.front() == ' ' || rest.front() == '\t' || rest.front() == '-';
    };
    return std::ranges::any_of(markers, is_marker);
}

// `// \ref{stable}, title` -> the title, capitalized, or empty.
std::string ref_title(std::string_view ref_line) {
    const std::size_t close = ref_line.find('}');
    if (close == std::string_view::npos)
        return {};
    const std::size_t comma = ref_line.find(',', close);
    if (comma == std::string_view::npos)
        return {};
    std::string title = trim(ref_line.substr(comma + 1));
    if (!title.empty())
        title.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(title.front())));
    return title;
}

// ---------------------------------------------------------------------------
// What a moved member becomes
// ---------------------------------------------------------------------------

// A `\ref` group of a class body.
struct Group {
    std::string stable;
    std::string title;
    bool        operator==(const Group&) const = default;
};

// One in-class definition the plan moves: where its in-class text is, the
// edits that reduce it to a declaration, and the block of text (comment lines
// and definition) that goes out of line.
struct Moved {
    unsigned             order = 0; // the declaration's offset: class-body order
    std::vector<Edit>    in_class;  // the splice to `;` and the comment removal
    std::string          block;     // comment lines, then the definition
    std::optional<Group> group;
};

// Where blocks are inserted. A point after a class's `;` takes "\n\n" + block;
// a point at the start of a line takes block + "\n\n".
enum class PointKind { AfterClass, AtLineStart };

struct Insertion {
    unsigned                 offset = 0;
    PointKind                kind   = PointKind::AtLineStart;
    std::vector<std::string> blocks;
};

// An existing `// \rSec<n>[stable]{...}` section after a class, and the
// offset at which a definition appended to it goes: the start of the next
// section header's line, or of the enclosing namespace's closing brace.
struct Section {
    int         depth = 0;
    std::string stable;
    unsigned    header = 0; // offset of the header comment
    unsigned    end    = 0;
};

// An out-of-line definition already in a section, for ordering: where its
// text begins (its comment's line, when it has one) and where its in-class
// declaration is.
struct Sibling {
    unsigned begin       = 0;
    unsigned declared_at = 0;
};

// Whether the written return type names anything an out-of-line return type
// can not see: a member of the class or of an enclosing one, the injected
// class name of a class template, a dependent name, or a decltype. Such a
// definition takes the trailing-return form, whose return type is looked up
// in class scope.
class ClassScopeMention : public clang::RecursiveASTVisitor<ClassScopeMention> {
  public:
    explicit ClassScopeMention(std::vector<const clang::CXXRecordDecl*> enclosing)
        : enclosing_(std::move(enclosing)) {}

    bool found() const { return found_; }

    bool VisitTypedefTypeLoc(clang::TypedefTypeLoc tl) { return note(tl.getDecl()); }
    bool VisitTagTypeLoc(clang::TagTypeLoc tl) { return note_tag(tl.getDecl()); }
    bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc tl) {
        // A dependent template name has no declaration: it is a member of a
        // dependent base or of the class, and either way needs class scope.
        const clang::TemplateDecl* tmpl = tl.getTypePtr()->getTemplateName().getAsTemplateDecl();
        return tmpl == nullptr ? flag() : note(tmpl);
    }
    bool VisitUsingTypeLoc(clang::UsingTypeLoc tl) { return note(tl.getDecl()); }
    bool VisitDependentNameTypeLoc(clang::DependentNameTypeLoc) { return flag(); }
    bool VisitUnresolvedUsingTypeLoc(clang::UnresolvedUsingTypeLoc) { return flag(); }
    bool VisitDecltypeTypeLoc(clang::DecltypeTypeLoc) { return flag(); }

  private:
    bool flag() {
        found_ = true;
        return false;
    }
    bool note(const clang::NamedDecl* decl) {
        if (decl != nullptr && decl->getDeclContext()->isRecord())
            return flag();
        return true;
    }
    bool note_tag(const clang::TagDecl* tag) {
        if (tag == nullptr)
            return true;
        if (tag->getDeclContext()->isRecord())
            return flag();
        // The class's own name, or an enclosing class's, as a type: the
        // injected class name. For a class template it is not visible before
        // the qualified declarator-id (`optional` there names the template).
        const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(tag);
        const bool  is_enclosing_template =
            record != nullptr && std::ranges::any_of(enclosing_, [&](const clang::CXXRecordDecl* e) {
                return e->getCanonicalDecl() == record->getCanonicalDecl() &&
                       (e->getDescribedClassTemplate() != nullptr ||
                        llvm::isa<clang::ClassTemplateSpecializationDecl>(e));
            });
        return is_enclosing_template ? flag() : true;
    }

    std::vector<const clang::CXXRecordDecl*> enclosing_;
    bool                                     found_ = false;
};

// ---------------------------------------------------------------------------
// The planner
// ---------------------------------------------------------------------------

class Planner {
  public:
    Planner(clang::ASTUnit& ast, const Options& options)
        : ctx_(ast.getASTContext()),
          src_(ast.getSourceManager(), ast.getLangOpts()),
          options_(options),
          regions_(conditional_regions(src_)) {}

    Rewrite run() {
        visit_scope(ctx_.getTranslationUnitDecl(), src_.size());
        return finish();
    }

  private:
    // ----- walking the file ------------------------------------------------

    // Namespace-level declarations of `scope`, whose text ends at `scope_end`
    // (the closing brace of a namespace, the end of the file for the
    // translation unit).
    void visit_scope(const clang::DeclContext* scope, unsigned scope_end) {
        std::ranges::for_each(scope->decls(), [&](const clang::Decl* decl) { visit_decl(decl, scope, scope_end); });
    }

    void visit_decl(const clang::Decl* decl, const clang::DeclContext* scope, unsigned scope_end) {
        if (decl->isImplicit() || !src_.in_main(decl->getLocation()))
            return;
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(decl)) {
            visit_scope(ns, src_.offset(ns->getRBraceLoc()));
            return;
        }
        if (const auto* linkage = llvm::dyn_cast<clang::LinkageSpecDecl>(decl)) {
            visit_scope(linkage, linkage->hasBraces() ? src_.offset(linkage->getRBraceLoc()) : scope_end);
            return;
        }
        if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl)) {
            if (tmpl->isThisDeclarationADefinition())
                outline_class(tmpl->getTemplatedDecl(), tmpl, scope, scope_end);
            return;
        }
        if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {
            // A specialization is its own definition, with its own members;
            // an implicit instantiation is not in the main file at all.
            if (record->isThisDeclarationADefinition())
                outline_class(record, record, scope, scope_end);
        }
    }

    // ----- one outermost class --------------------------------------------

    void outline_class(const clang::CXXRecordDecl* record,
                       const clang::Decl*          whole,
                       const clang::DeclContext*   scope,
                       unsigned                    scope_end) {
        if (record->getName().empty()) {
            refuse_all(record, "the class is unnamed, so its members have no qualified name");
            return;
        }
        const unsigned     class_begin = src_.offset(whole->getBeginLoc());
        const unsigned     class_end   = after_semicolon(record);
        std::vector<Moved> moved;
        collect(record, class_begin, class_end, moved);
        if (moved.empty())
            return;
        std::ranges::stable_sort(moved, {}, &Moved::order);
        std::ranges::for_each(
            moved, [&](const Moved& m) { edits_.insert(edits_.end(), m.in_class.begin(), m.in_class.end()); });
        moved_ += static_cast<unsigned>(moved.size());

        foundation::match(
            options_.comments,
            foundation::when<Comments::Keep>([&] { place_after_class(class_end, moved); }),
            foundation::when<Comments::Move>([&] { place_by_section(class_end, scope, scope_end, moved); }));
    }

    // Every in-class definition of `record` and its nested classes, in
    // class-body order.
    void
    collect(const clang::CXXRecordDecl* record, unsigned class_begin, unsigned class_end, std::vector<Moved>& out) {
        const auto member = [&](const clang::Decl* decl) {
            if (decl->isImplicit() || llvm::isa<clang::FriendDecl>(decl))
                return; // a hidden friend stays, by construction
            if (const auto* nested = llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {
                if (nested->isInjectedClassName() || !nested->isThisDeclarationADefinition())
                    return;
                if (nested->getName().empty())
                    refuse_all(nested, "the class is unnamed, so its members have no qualified name");
                else
                    collect(nested, class_begin, class_end, out);
                return;
            }
            if (const auto* nested_tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl)) {
                if (nested_tmpl->isThisDeclarationADefinition())
                    collect(nested_tmpl->getTemplatedDecl(), class_begin, class_end, out);
                return;
            }
            const clang::FunctionDecl* fn = frontend::member_function_or_template(decl);
            if (fn == nullptr || !is_in_class_definition(fn, record))
                return;
            if (std::optional<Moved> m = plan_member(record, decl, fn, class_begin, class_end))
                out.push_back(std::move(*m));
        };
        std::ranges::for_each(record->decls(), member);
    }

    static bool is_in_class_definition(const clang::FunctionDecl* fn, const clang::CXXRecordDecl* record) {
        return fn->doesThisDeclarationHaveABody() && !fn->isDefaulted() && !fn->isDeletedAsWritten() &&
               fn->getLexicalDeclContext() == record && fn->getBody() != nullptr;
    }

    void refuse_all(const clang::CXXRecordDecl* record, std::string_view reason) {
        const auto refuse_member = [&](const clang::Decl* decl) {
            const clang::FunctionDecl* fn = frontend::member_function_or_template(decl);
            if (fn != nullptr && !llvm::isa<clang::FriendDecl>(decl) && is_in_class_definition(fn, record))
                refuse(src_.offset(fn->getLocation()), reason);
        };
        std::ranges::for_each(record->decls(), refuse_member);
    }

    void refuse(unsigned offset, std::string_view reason) {
        refusals_.push_back({src_.line_of(offset), std::string(reason)});
    }

    // ----- one member ------------------------------------------------------

    std::optional<Moved> plan_member(const clang::CXXRecordDecl* owner,
                                     const clang::Decl*          whole,
                                     const clang::FunctionDecl*  fn,
                                     unsigned                    class_begin,
                                     unsigned                    class_end) {
        const clang::Stmt* body = fn->getBody();
        if (touches_macro(whole, fn, body)) {
            refuse(src_.offset(fn->getLocation()), "the definition touches a macro expansion");
            return std::nullopt;
        }
        const unsigned decl_begin = src_.offset(whole->getBeginLoc());
        const unsigned body_end   = src_.end_of(body->getEndLoc());
        if (const std::optional<Region> guard = partial_guard(decl_begin, body_end, class_begin, class_end)) {
            refuse(src_.offset(fn->getLocation()),
                   std::format("the definition is inside a conditional-compilation region (line {}) that does not "
                               "enclose the whole class",
                               src_.line_of(guard->begin)));
            return std::nullopt;
        }

        Moved m;
        m.order = decl_begin;

        // The declaration left in class: everything up to the body (or the
        // `:` of a written mem-initializer list), then `;`.
        const unsigned tail      = frontend::spliced_tail_begin(fn, body, src_.sm());
        const unsigned decl_tail = src_.trim_back(tail);
        m.in_class.push_back({decl_tail, body_end, ";"});

        // The in-class indentation, to take off everything that moves.
        const unsigned    first_line = src_.line_start(decl_begin);
        const std::string indent =
            src_.blank_between(first_line, decl_begin) ? src_.text(first_line, decl_begin) : std::string{};

        std::string block;
        if (options_.comments == Comments::Move) {
            if (const std::optional<Region> comment = moving_comment_lines(whole, decl_begin)) {
                block += dedent(src_.text(comment->begin, src_.trim_back(comment->end)), indent) + "\n";
                m.in_class.push_back({comment->begin, comment->end, ""});
            }
            m.group = group_of(owner, decl_begin);
        }
        block += definition_text(owner, fn, decl_begin, tail, body_end, indent);
        m.block = std::move(block);
        return m;
    }

    static bool touches_macro(const clang::Decl* whole, const clang::FunctionDecl* fn, const clang::Stmt* body) {
        return whole->getBeginLoc().isMacroID() || fn->getLocation().isMacroID() || fn->getEndLoc().isMacroID() ||
               body->getBeginLoc().isMacroID() || body->getEndLoc().isMacroID();
    }

    std::optional<Region> partial_guard(unsigned begin, unsigned end, unsigned class_begin, unsigned class_end) const {
        const auto intersects_not_enclosing = [&](const Region& r) {
            const bool intersects = r.begin < end && begin < r.end;
            const bool encloses   = r.begin <= class_begin && class_end <= r.end;
            return intersects && !encloses;
        };
        const auto found = std::ranges::find_if(regions_, intersects_not_enclosing);
        if (found == regions_.end())
            return std::nullopt;
        return *found;
    }

    // ----- the out-of-line definition ---------------------------------------

    // The out-of-line definition. The copied parts (the declaration proper and
    // the body) lose `indent`, their in-class indentation; the heads the tool
    // writes are laid out for namespace scope already.
    std::string definition_text(const clang::CXXRecordDecl* owner,
                                const clang::FunctionDecl*  fn,
                                unsigned                    decl_begin,
                                unsigned                    tail,
                                unsigned                    body_end,
                                std::string_view            indent) const {
        std::string                        def;
        const std::vector<std::string>     heads    = template_heads(owner);
        const clang::FunctionTemplateDecl* own_tmpl = fn->getDescribedFunctionTemplate();
        std::ranges::for_each(heads, [&](const std::string& h) { def += h + "\n"; });
        unsigned prefix_begin = decl_begin;
        if (own_tmpl != nullptr) {
            def += head_without_defaults(own_tmpl->getTemplateParameters()) + "\n";
            prefix_begin = src_.offset(fn->getBeginLoc()); // past the member's own head, rebuilt above
        }
        const bool any_head = !heads.empty() || own_tmpl != nullptr;
        if (!any_head && !fn->isConstexprSpecified() && !fn->isConsteval() && !fn->isInlineSpecified())
            def += "inline ";

        // The decl-specifiers and return type, with the padding a formatter
        // put between a declaration's type and its name (AlignConsecutive-
        // Declarations) collapsed: the alignment was with neighbours that
        // are not moving.
        const unsigned name_begin = src_.offset(fn->getNameInfo().getBeginLoc());
        std::string    trailing;
        if (needs_trailing_return(owner, fn)) {
            const clang::SourceRange rt = fn->getReturnTypeSourceRange();
            const unsigned           rb = src_.offset(rt.getBegin());
            const unsigned           re = src_.end_of(rt.getEnd());
            def += collapse_blanks(drop_specifiers(prefix_begin, rb) + "auto " + drop_specifiers(re, name_begin));
            trailing = " -> " + src_.text(rb, re);
        } else {
            def += collapse_blanks(drop_specifiers(prefix_begin, name_begin));
        }
        def += qualifier(owner);
        def += dedent(signature_text(fn, name_begin, tail), indent);
        def += trailing;
        def += " " + dedent(src_.text(tail, body_end), indent);
        return def;
    }

    // Runs of blanks to one space.
    static std::string collapse_blanks(std::string_view text) {
        std::string out;
        const auto  keep = [&](char c) {
            const bool blank = c == ' ' || c == '\t';
            return !(blank && !out.empty() && out.back() == ' ');
        };
        std::ranges::for_each(text | std::views::filter(keep), [&](char c) { out.push_back(c == '\t' ? ' ' : c); });
        return out;
    }

    // [begin, end) of the source with `static`, `virtual`, `explicit` and
    // `explicit(...)`, and `[[...]]` attributes removed: the parts of a
    // decl-specifier-seq that belong to the first declaration only.
    std::string drop_specifiers(unsigned begin, unsigned end) const {
        std::string out;
        unsigned    cursor         = begin;
        const auto  toks           = src_.tokens_in(begin, end);
        const auto  matching_close = [&](auto open_it, std::string_view open, std::string_view close) {
            int  depth = 0;
            auto it    = open_it;
            // substrate generic algorithm: a bracket matcher, the depth
            // being its state.
            for (; it != toks.end(); ++it) {
                if (it->spelling == open)
                    ++depth;
                if (it->spelling == close && --depth == 0)
                    break;
            }
            return it;
        };
        // substrate generic algorithm: a scan that skips ahead over the
        // tokens it drops, so the position advances from inside the body.
        for (auto it = toks.begin(); it != toks.end(); ++it) {
            const bool is_specifier =
                it->spelling == "static" || it->spelling == "virtual" || it->spelling == "explicit";
            const bool is_attribute =
                it->spelling == "[" && std::next(it) != toks.end() && std::next(it)->spelling == "[";
            if (!is_specifier && !is_attribute)
                continue;
            out += src_.text(cursor, it->begin);
            auto last = it;
            if (is_attribute) {
                last = matching_close(it, "[", "]");
            } else if (it->spelling == "explicit" && std::next(it) != toks.end() && std::next(it)->spelling == "(") {
                last = matching_close(std::next(it), "(", ")");
            }
            if (last == toks.end())
                last = std::prev(toks.end());
            unsigned skip_to = last->end;
            // and the blank after it, so `static int f` reads `int f`
            while (skip_to < end && (src_.buffer()[skip_to] == ' ' || src_.buffer()[skip_to] == '\t'))
                ++skip_to; // substrate generic algorithm: a find_if_not over bytes, inline for the offset it leaves
            cursor = skip_to;
            it     = last;
        }
        out += src_.text(cursor, end);
        return out;
    }

    // The declarator-id through the end of the declaration proper: name,
    // parameters without default arguments, qualifiers, noexcept, a trailing
    // requires-clause; `override` and `final` dropped, since they belong to
    // the in-class declaration alone.
    std::string signature_text(const clang::FunctionDecl* fn, unsigned name_begin, unsigned tail) const {
        std::string sig = src_.text(name_begin, tail);
        // Cuts, back to front so earlier offsets hold.
        std::vector<Region> cuts;
        std::ranges::for_each(fn->parameters(), [&](const clang::ParmVarDecl* p) {
            if (!p->hasDefaultArg() || p->hasInheritedDefaultArg())
                return;
            const clang::SourceRange r  = p->getDefaultArgRange();
            const unsigned           ab = src_.offset(r.getBegin());
            const unsigned           ae = src_.end_of(r.getEnd());
            const unsigned           eq = src_.trim_back(ab);
            if (eq == 0 || src_.buffer()[eq - 1] != '=')
                return;
            cuts.push_back({src_.trim_back(eq - 1), ae});
        });
        std::ranges::for_each(virt_specifiers(fn, name_begin, tail), [&](const Region& r) { cuts.push_back(r); });
        std::ranges::sort(cuts, std::ranges::greater{}, &Region::begin);
        std::ranges::for_each(cuts, [&](const Region& c) { sig.erase(c.begin - name_begin, c.end - c.begin); });
        return trim(sig);
    }

    // `override` and `final` after the parameter clause, each with the blank
    // before it.
    std::vector<Region> virt_specifiers(const clang::FunctionDecl* fn, unsigned name_begin, unsigned tail) const {
        std::vector<Region> out;
        if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(fn); method == nullptr || !method->isVirtual())
            return out;
        const auto toks = src_.tokens_in(name_begin, tail);
        std::ranges::for_each(toks, [&](const Tok& t) {
            if (t.spelling == "override" || t.spelling == "final")
                out.push_back({src_.trim_back(t.begin), t.end});
        });
        return out;
    }

    bool needs_trailing_return(const clang::CXXRecordDecl* owner, const clang::FunctionDecl* fn) const {
        if (llvm::isa<clang::CXXConstructorDecl>(fn) || llvm::isa<clang::CXXDestructorDecl>(fn) ||
            llvm::isa<clang::CXXConversionDecl>(fn))
            return false;
        if (const auto* proto = fn->getType()->getAs<clang::FunctionProtoType>();
            proto != nullptr && proto->hasTrailingReturn())
            return false;
        const clang::TypeSourceInfo* tsi = fn->getTypeSourceInfo();
        if (tsi == nullptr)
            return false;
        const auto fn_loc = tsi->getTypeLoc().getAsAdjusted<clang::FunctionTypeLoc>();
        if (fn_loc.isNull())
            return false;
        ClassScopeMention visitor(enclosing_records(owner));
        visitor.TraverseTypeLoc(fn_loc.getReturnLoc());
        return visitor.found();
    }

    static std::vector<const clang::CXXRecordDecl*> enclosing_records(const clang::CXXRecordDecl* owner) {
        std::vector<const clang::CXXRecordDecl*> out;
        // substrate generic algorithm: a walk up the enclosing records, which
        // have no range.
        for (const clang::CXXRecordDecl* r = owner; r != nullptr;
             r                             = llvm::dyn_cast<clang::CXXRecordDecl>(r->getDeclContext()))
            out.push_back(r);
        return out;
    }

    // `template <...>` for `params`, default arguments removed and the head's
    // requires-clause repeated.
    std::string head_without_defaults(const clang::TemplateParameterList* params) const {
        const auto one = [&](const clang::NamedDecl* p) {
            const unsigned b     = src_.offset(p->getBeginLoc());
            const unsigned e     = src_.end_of(p->getEndLoc());
            int            depth = 0;
            unsigned       cut   = e;
            // substrate generic algorithm: a bracket-depth scan for the
            // first `=` at depth zero, the default argument's.
            for (const Tok& t : src_.tokens_in(b, e)) {
                if (t.spelling == "<" || t.spelling == "(" || t.spelling == "[" || t.spelling == "{")
                    ++depth;
                if (t.spelling == ">" || t.spelling == ")" || t.spelling == "]" || t.spelling == "}")
                    --depth;
                if (depth == 0 && t.spelling == "=") {
                    cut = t.begin;
                    break;
                }
            }
            return trim(src_.text(b, cut));
        };
        std::string head = "template <";
        head += std::views::transform(*params, one) | std::views::join_with(std::string(", ")) |
                std::ranges::to<std::string>();
        head += ">";
        if (const clang::Expr* rc = params->getRequiresClause())
            head += "\n    requires " + src_.text(src_.offset(rc->getBeginLoc()), src_.end_of(rc->getEndLoc()));
        return head;
    }

    // The heads of the enclosing class templates, outermost first. A partial
    // specialization contributes its own head; an explicit specialization
    // none, since its members take no `template<>`.
    std::vector<std::string> template_heads(const clang::CXXRecordDecl* record) const {
        std::vector<std::string> heads;
        if (const auto* outer = llvm::dyn_cast<clang::CXXRecordDecl>(record->getDeclContext()))
            heads = template_heads(outer);
        if (const auto* partial = llvm::dyn_cast<clang::ClassTemplatePartialSpecializationDecl>(record))
            heads.push_back(head_without_defaults(partial->getTemplateParameters()));
        else if (llvm::isa<clang::ClassTemplateSpecializationDecl>(record))
            return heads;
        else if (const clang::ClassTemplateDecl* tmpl = record->getDescribedClassTemplate())
            heads.push_back(head_without_defaults(tmpl->getTemplateParameters()));
        return heads;
    }

    // `outer<T, Alloc>::inner::`.
    std::string qualifier(const clang::CXXRecordDecl* record) const {
        std::string q;
        if (const auto* outer = llvm::dyn_cast<clang::CXXRecordDecl>(record->getDeclContext()))
            q = qualifier(outer);
        q += record->getNameAsString();
        if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
            if (const clang::ASTTemplateArgumentListInfo* args = spec->getTemplateArgsAsWritten())
                q += src_.text(src_.offset(args->getLAngleLoc()), src_.end_of(args->getRAngleLoc()));
        } else if (const clang::ClassTemplateDecl* tmpl = record->getDescribedClassTemplate()) {
            const auto use = [](const clang::NamedDecl* p) {
                return p->getNameAsString() + (p->isParameterPack() ? "..." : "");
            };
            q += "<";
            q += std::views::transform(*tmpl->getTemplateParameters(), use) |
                 std::views::join_with(std::string(", ")) | std::ranges::to<std::string>();
            q += ">";
        }
        return q + "::";
    }

    // ----- comments ----------------------------------------------------------

    // The lines of the comment attached to `fn` that travel with the
    // definition under --move-comments, as the byte range of whole lines, or
    // nothing: the block from its first `//!`/`/*!` line to its end, unless
    // that block is declaration markers only, the comment is not directly
    // above the declaration, or it is a trailing comment.
    std::optional<Region> moving_comment_lines(const clang::Decl* whole, unsigned decl_begin) const {
        // Looked up through the described template, as the front end does
        // (docblock_lookup.hpp explains the anchor rule).
        const clang::RawComment* rc = frontend::attached_raw_comment(whole);
        if (rc == nullptr || rc->isTrailingComment() || !src_.in_main(rc->getBeginLoc()))
            return std::nullopt;
        const unsigned cb = src_.offset(rc->getBeginLoc());
        const unsigned ce = src_.end_of(rc->getEndLoc());
        // Only whitespace may lie between the comment and the declaration:
        // Clang attaches across an access specifier, this does not.
        if (!src_.blank_between(ce, decl_begin))
            return std::nullopt;

        // Its lines. A first line with code before the comment is a trailing
        // comment of that code, coalesced in; it never opens the run.
        std::vector<unsigned> starts{src_.line_start(cb)};
        // substrate generic algorithm: an unfold of the line starts inside
        // [cb, ce), which the buffer has no view for.
        for (unsigned next = src_.next_line_start(cb); next < ce; next = src_.next_line_start(next))
            starts.push_back(next);
        const bool first_is_trailing = !src_.blank_between(starts.front(), cb);
        const auto line_text         = [&](unsigned start) {
            return std::string_view(src_.buffer()).substr(start, src_.next_line_start(start) - start);
        };
        const auto candidates = std::span(starts).subspan(first_is_trailing ? 1 : 0);
        const auto first_markup =
            std::ranges::find_if(candidates, [&](unsigned start) { return starts_markup(line_text(start)); });
        if (first_markup == candidates.end())
            return std::nullopt;
        const std::span moving(first_markup, candidates.end());
        if (std::ranges::all_of(moving, [&](unsigned start) { return is_declaration_marker_line(line_text(start)); }))
            return std::nullopt;
        return Region{*first_markup, src_.next_line_start(ce)};
    }

    // The nearest `// \ref{stable}, title` comment above `offset` in the body
    // of `owner`.
    std::optional<Group> group_of(const clang::CXXRecordDecl* owner, unsigned offset) const {
        const auto* comments = ctx_.Comments.getCommentsInFile(src_.main());
        if (comments == nullptr)
            return std::nullopt;
        const unsigned       body_begin = src_.offset(owner->getBraceRange().getBegin());
        std::optional<Group> group;
        // substrate generic algorithm: the last match wins, over an ordered
        // map whose range has no reverse find with a projection to the
        // comment's first line.
        for (const auto& [at, comment] : *comments) {
            if (at < body_begin)
                continue;
            if (at >= offset)
                break;
            const std::string      raw        = comment->getRawText(src_.sm()).str();
            const std::string_view first_line = std::string_view(raw).substr(0, raw.find('\n'));
            if (const std::optional<std::string> stable = grammar::parse_ref(first_line))
                group = Group{*stable, ref_title(first_line)};
        }
        return group;
    }

    // ----- placement ----------------------------------------------------------

    void place_after_class(unsigned class_end, const std::vector<Moved>& moved) {
        Insertion& point = insertion(class_end, PointKind::AfterClass);
        std::ranges::for_each(moved, [&](const Moved& m) { point.blocks.push_back(m.block); });
    }

    void place_by_section(unsigned                  class_end,
                          const clang::DeclContext* scope,
                          unsigned                  scope_end,
                          const std::vector<Moved>& moved) {
        const std::vector<Section> sections = sections_after(class_end, scope_end);
        const unsigned  new_sections_at     = sections.empty() ? class_end : src_.line_start(sections.back().end);
        const PointKind new_sections_kind   = sections.empty() ? PointKind::AfterClass : PointKind::AtLineStart;
        const int       depth               = sections.empty() ? options_.section_depth : common_depth(sections);

        // Groups with no section yet, in class-body order of first use.
        std::vector<Group> fresh;
        const auto         place_one = [&](const Moved& m) {
            if (!m.group) {
                insertion(class_end, PointKind::AfterClass).blocks.push_back(m.block);
                return;
            }
            const auto section = std::ranges::find(sections, m.group->stable, &Section::stable);
            if (section == sections.end()) {
                if (!std::ranges::contains(fresh, *m.group)) {
                    fresh.push_back(*m.group);
                    insertion(new_sections_at, new_sections_kind)
                        .blocks.push_back(
                            std::format("// \\rSec{}[{}]{{{}}}", depth, m.group->stable, m.group->title));
                }
                insertion(new_sections_at, new_sections_kind).blocks.push_back(m.block);
                return;
            }
            const std::vector<Sibling> siblings = siblings_in(*section, scope);
            const auto                 later =
                std::ranges::find_if(siblings, [&](const Sibling& s) { return s.declared_at > m.order; });
            const unsigned at = later == siblings.end() ? section->end : later->begin;
            insertion(at, PointKind::AtLineStart).blocks.push_back(m.block);
        };
        std::ranges::for_each(moved, place_one);
    }

    Insertion& insertion(unsigned offset, PointKind kind) {
        const auto found = std::ranges::find(insertions_, offset, &Insertion::offset);
        if (found != insertions_.end())
            return *found;
        insertions_.push_back({offset, kind, {}});
        return insertions_.back();
    }

    // The `\rSec` sections between `class_end` and `scope_end`, with each
    // one's end: the start of the next header's line of its own depth or
    // shallower, else the line of the scope's closing brace.
    std::vector<Section> sections_after(unsigned class_end, unsigned scope_end) const {
        std::vector<Section> sections;
        const auto*          comments = ctx_.Comments.getCommentsInFile(src_.main());
        if (comments == nullptr)
            return sections;
        const auto in_range = [&](const auto& entry) { return entry.first > class_end && entry.first < scope_end; };
        std::ranges::for_each(*comments | std::views::filter(in_range), [&](const auto& entry) {
            const std::string raw = entry.second->getRawText(src_.sm()).str();
            if (const auto header = grammar::parse_rsec(std::string_view(raw).substr(0, raw.find('\n'))))
                sections.push_back({header->value.depth, header->value.stable, entry.first, scope_end});
        });
        const auto close = [&](Section& s) {
            const auto next = std::ranges::find_if(
                sections, [&](const Section& t) { return t.header > s.header && t.depth <= s.depth; });
            s.end = src_.line_start(next == sections.end() ? scope_end : next->header);
        };
        std::ranges::for_each(sections, close);
        return sections;
    }

    static int common_depth(const std::vector<Section>& sections) {
        std::map<int, int> counts;
        std::ranges::for_each(sections, [&](const Section& s) { ++counts[s.depth]; });
        return std::ranges::max_element(counts, {}, &std::pair<const int, int>::second)->first;
    }

    // The out-of-line member definitions already inside `section`, with
    // where each one's in-class declaration is.
    std::vector<Sibling> siblings_in(const Section& section, const clang::DeclContext* scope) const {
        std::vector<Sibling> siblings;
        const auto           one = [&](const clang::Decl* decl) {
            const clang::FunctionDecl* fn = frontend::member_function_or_template(decl);
            if (fn == nullptr || !fn->doesThisDeclarationHaveABody() || !src_.in_main(decl->getLocation()))
                return;
            const unsigned begin = src_.offset(decl->getBeginLoc());
            if (begin <= section.header || begin >= section.end)
                return;
            const clang::FunctionDecl* canonical = fn->getCanonicalDecl();
            if (canonical == fn || !llvm::isa<clang::CXXRecordDecl>(canonical->getLexicalDeclContext()) ||
                !src_.in_main(canonical->getLocation()))
                return;
            unsigned text_begin = src_.line_start(begin);
            if (const clang::RawComment* rc = frontend::attached_raw_comment(decl);
                rc != nullptr && src_.in_main(rc->getBeginLoc()) && src_.offset(rc->getBeginLoc()) < begin)
                text_begin = src_.line_start(src_.offset(rc->getBeginLoc()));
            siblings.push_back({text_begin, src_.offset(canonical->getLocation())});
        };
        std::ranges::for_each(scope->decls(), one);
        std::ranges::sort(siblings, {}, &Sibling::begin);
        return siblings;
    }

    // ----- assembling the result ----------------------------------------------

    unsigned after_semicolon(const clang::CXXRecordDecl* record) const {
        const clang::SourceLocation       rbrace = src_.sm().getFileLoc(record->getBraceRange().getEnd());
        const std::optional<clang::Token> semi   = clang::Lexer::findNextToken(rbrace, src_.sm(), src_.lang_opts());
        if (semi && semi->is(clang::tok::semi))
            return src_.offset(semi->getLocation()) + 1;
        return src_.end_of(rbrace);
    }

    Rewrite finish() {
        const auto render = [](const Insertion& point) {
            const auto piece = [&](const std::string& block) {
                return foundation::match(point.kind,
                                         foundation::when<PointKind::AfterClass>([&] { return "\n\n" + block; }),
                                         foundation::when<PointKind::AtLineStart>([&] { return block + "\n\n"; }));
            };
            return Edit{point.offset,
                        point.offset,
                        point.blocks | std::views::transform(piece) | std::views::join |
                            std::ranges::to<std::string>()};
        };
        std::ranges::for_each(insertions_ | std::views::transform(render),
                              [&](Edit e) { edits_.push_back(std::move(e)); });
        std::ranges::stable_sort(edits_, {}, &Edit::begin);
        std::ranges::sort(refusals_, {}, &Refusal::line);
        return Rewrite{std::move(edits_), std::move(refusals_), moved_};
    }

    const clang::ASTContext& ctx_;
    Source                   src_;
    Options                  options_;
    std::vector<Region>      regions_;
    std::vector<Edit>        edits_;
    std::vector<Insertion>   insertions_;
    std::vector<Refusal>     refusals_;
    unsigned                 moved_ = 0;
};

} // namespace

} // namespace beman::specgen::split_inline

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::split_inline::PointKind> = std::array{
    beman::specgen::split_inline::PointKind::AfterClass,
    beman::specgen::split_inline::PointKind::AtLineStart,
};

namespace beman::specgen::split_inline {

Rewrite plan_rewrite(clang::ASTUnit& ast, const Options& options) { return Planner(ast, options).run(); }

std::string apply_edits(std::string_view source, const std::vector<Edit>& edits) {
    struct State {
        std::string out;
        unsigned    cursor = 0;
    };
    const auto apply = [&](State s, const Edit& e) {
        if (e.begin < s.cursor || e.end > source.size() || e.end < e.begin)
            return s; // overlapping or out of range: the planner never emits one
        s.out += source.substr(s.cursor, e.begin - s.cursor);
        s.out += e.text;
        s.cursor = e.end;
        return s;
    };
    State s = std::ranges::fold_left(edits, State{}, apply);
    s.out += source.substr(s.cursor);
    return std::move(s.out);
}

} // namespace beman::specgen::split_inline
