// src/beman/specgen/frontend/frontend.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/frontend.hpp>

#include <beman/specgen/conjuncts.hpp>
#include <beman/specgen/docblock.hpp>
#include <beman/specgen/document_build.hpp>
#include <beman/specgen/foundation/parse/cursor.hpp>
#include <beman/specgen/foundation/parse/parser.hpp>
#include <beman/specgen/lower.hpp>

// GCC reports -Wnonnull eight times from inside LLVM's own headers when it
// inlines RecursiveASTVisitor::TraverseCXXRecordHelper for the visitors below:
// LazyOffsetPtr::get performs a pointer-to-member call on an
// ExternalASTSource* that is null whenever there is no PCH or module, which is
// exactly when the branch is not taken -- a precondition GCC cannot see
// through. -isystem does not silence it, because the diagnostic comes from the
// middle end after inlining rather than from the parse, so the suppression is
// scoped to the statements these headers contribute and our own code stays
// fully checked.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/RawCommentList.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Format/Format.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessingRecord.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Core/Replacement.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cctype>
#include <concepts>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// Short alias for the combinator library (decision parser-combinators) used
// by parse_rsec, below.
namespace parse = beman::specgen::foundation::parse;

SmokeResult smoke_check(std::string_view source) {
    SmokeResult result;

    // Tooling surface (design §3.1): parse into an AST with the flags the real
    // front end will use. -fsyntax-only keeps it to a parse; -fparse-all-comments
    // is what the raw-comment collection needs, exercised here so the flag is
    // proven wired.
    const std::vector<std::string>  args = {"-std=c++2c", "-fsyntax-only", "-fparse-all-comments"};
    std::unique_ptr<clang::ASTUnit> ast  = clang::tooling::buildASTFromCodeWithArgs(std::string(source), args);
    result.ast_built                     = ast != nullptr;

    // Formatting surface (design §3.6): run clang-format over a fixed snippet.
    // FormatStyle tuning to draft conventions is draft_format_style(), below;
    // here it only proves the surface links and produces output.
    const std::string                        code   = "int  f(  int x ){return  x ;}";
    const clang::format::FormatStyle         style  = clang::format::getLLVMStyle();
    const std::vector<clang::tooling::Range> ranges = {clang::tooling::Range(0, static_cast<unsigned>(code.size()))};
    const clang::tooling::Replacements       replacements = clang::format::reformat(style, code, ranges);
    if (llvm::Expected<std::string> formatted = clang::tooling::applyAllReplacements(code, replacements))
        result.formatted = *formatted;
    else
        llvm::consumeError(formatted.takeError());

    return result;
}

// --- compiler-argument sourcing ---------------------------------------------
// A corpus header never needs an include path — every one is self-contained
// (decision hermetic-corpus) — but a header outside the corpus (the real
// beman/optional/optional.hpp) can reach outside itself. ParseOptions
// (frontend.hpp) names the three possible sources; these two functions are
// what parse_header (below) and the driver share to turn one into the actual
// argument vector.

std::vector<std::string> filter_compile_command_args(const std::vector<std::string>& command_line,
                                                     const std::string&              filename) {
    // A stateful filter: dropping `-o <file>` has to consume the *next*
    // element too, which a plain views::filter predicate cannot express (it
    // sees one element at a time, never two). This is a fold building the
    // surviving vector, threading "skip the next token" as the one bit of
    // state the shape needs — every read is still at the current position,
    // so this is not the scatter the zip/index rule reserves a raw loop for.
    struct State {
        std::vector<std::string> out;
        bool                     skip_next = false;
    };
    // element 0 is the compiler itself (argv[0]), dropped by starting the
    // fold past it rather than by a branch inside the step function.
    State result = std::ranges::fold_left(command_line | std::views::drop(command_line.empty() ? 0 : 1),
                                          State{},
                                          [&](State state, const std::string& arg) {
                                              if (state.skip_next) {
                                                  state.skip_next = false;
                                                  return state;
                                              }
                                              // A bare "--" is not a flag: it is clang tooling's own
                                              // end-of-options sentinel, which `inferMissingCompileCommands`
                                              // appends (as `{"--", filename}`) when a database is asked about
                                              // a file it has no literal entry for -- see this function's doc
                                              // comment for why leaving it in corrupts the parse rather than
                                              // merely doing nothing.
                                              if (arg == filename || arg == "-c" || arg == "--")
                                                  return state;
                                              if (arg == "-o") {
                                                  state.skip_next = true;
                                                  return state;
                                              }
                                              state.out.push_back(arg);
                                              return state;
                                          });
    return std::move(result.out);
}

ResolvedArgs resolve_extra_args(std::string_view header_path, const ParseOptions& options) {
    // A caller who spelled flags is not asking to be second-guessed.
    if (!options.extra_args.empty())
        return ResolvedArgs{options.extra_args, {}};

    // At most one of these two is even attempted, and the order is
    // precedence, not preference — an explicit directory beats autodetection
    // whenever both happen to be set at once.
    std::unique_ptr<clang::tooling::CompilationDatabase> db;
    std::string                                          error;
    if (!options.compile_commands_dir.empty())
        db = clang::tooling::CompilationDatabase::loadFromDirectory(options.compile_commands_dir, error);
    else if (options.probe_compile_commands)
        db = clang::tooling::CompilationDatabase::autoDetectFromSource(std::string(header_path), error);

    if (!db)
        return {};

    // The first entry for this file; no entry is not an error — fall
    // through to the caller's fixed defaults, the same as no database at all.
    const std::vector<clang::tooling::CompileCommand> commands = db->getCompileCommands(std::string(header_path));
    if (commands.empty())
        return {};

    const clang::tooling::CompileCommand& command = commands.front();
    return ResolvedArgs{filter_compile_command_args(command.CommandLine, command.Filename), command.Directory};
}

namespace {

namespace ir = beman::specgen::ir;

// Result of parse_header: `ast` is null exactly when the header could
// not be read or Clang could not build any AST from it at all;
// `had_error` reports whether
// Clang's own DiagnosticsEngine flagged an error somewhere during the parse
// that *did* produce `ast`. The two are independent — buildASTFromCodeWithArgs
// recovers as best it can after a fatal preprocessor error (an unsatisfiable
// `#include`, the corpus case tests/corpus/include_path exercises) and still
// hands back a non-null ASTUnit, so `ast != nullptr` alone cannot tell a clean
// parse from a partial one. hasErrorOccurred(), not
// hasUncompilableErrorOccurred(): the latter is about a later codegen stage
// this `-fsyntax-only` tier never reaches, so it is the wrong question for a
// syntax-only parse to ask.
struct ParsedHeader {
    std::unique_ptr<clang::ASTUnit> ast;
    bool                            had_error = false;
};

// Shared parse path (design §3.1): slurp the header as text — buildASTFromCodeWithArgs
// takes the source by value and a separate display path, rather than opening the file
// itself (a real compilation database's ClangTool would; this tier does not stand one
// up) — then build the AST with the fixed args every front-end entry point
// uses, plus whatever `options` resolves to. `ast` is null on either
// failure so callers share one "give up" check; see ParsedHeader for why a
// non-null `ast` is not by itself "the parse went fine".
ParsedHeader parse_header(std::string_view header_path, const ParseOptions& options) {
    std::ifstream in(std::string(header_path), std::ios::binary);
    if (!in)
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();

    // specgen's own -std default first (a project's own -std, if it
    // supplies one, overrides it — clang's last-wins argument parsing makes
    // this ordering meaningful), the resolved args in the middle, and the two
    // structural requirements last, where that same last-wins parsing
    // protects them from being overridden by anything a caller supplies.
    std::vector<std::string> args = {"-std=c++2c"};
    args.append_range(resolve_extra_args(header_path, options).args);
    args.push_back("-fsyntax-only");
    args.push_back("-fparse-all-comments");
    args.push_back("-Xclang");
    args.push_back("-detailed-preprocessing-record");
    std::unique_ptr<clang::ASTUnit> ast =
        clang::tooling::buildASTFromCodeWithArgs(buffer.str(), args, std::string(header_path));
    const bool had_error = ast && ast->getDiagnostics().hasErrorOccurred();
    return ParsedHeader{std::move(ast), had_error};
}

// Namespaces (and, incidentally, `extern "C"` blocks) are transparent scoping
// constructs: their members are what design §3.1 means by "top-level decls",
// not the enclosing namespace itself. top_level_begin()/end() yields the
// NamespaceDecl, so descend through it and its ilk; a class or function found
// this way (or directly at the top level) is a document-tree item in its own
// right and is not descended into further — its members are not top-level.
//
// Collects the raw clang::Decl* (rather than a label) so that classify()
// (stage 1 of the document-build pipeline) can classify each one;
// collect_interleaved formats a label from it.
void collect_top_level_decl(clang::Decl*                decl,
                            const clang::SourceManager& sm,
                            clang::FileID               main_file,
                            std::vector<clang::Decl*>&  out) {
    if (auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(decl)) {
        // substrate generic algorithm: recursive descent over a decl tree --
        // the recursion is the algorithm, and this loop only feeds it one
        // child at a time; it is not a fold over a range that already
        // exists, and this tree declines ranges::for_each as a costume for a
        // loop whose job is a side effect (the recursive call), not building
        // a container.
        for (clang::Decl* child : ns->decls())
            collect_top_level_decl(child, sm, main_file, out);
        return;
    }
    if (auto* linkage = llvm::dyn_cast<clang::LinkageSpecDecl>(decl)) {
        // substrate generic algorithm: same recursive tree descent as above.
        for (clang::Decl* child : linkage->decls())
            collect_top_level_decl(child, sm, main_file, out);
        return;
    }

    const clang::SourceLocation loc = decl->getBeginLoc();
    if (!loc.isValid())
        return;
    const auto [file_id, offset] = sm.getDecomposedLoc(loc);
    if (file_id != main_file)
        return;

    out.push_back(decl);
}

std::string decl_label(clang::Decl* decl) {
    std::string label = decl->getDeclKindName();
    if (const auto* named = llvm::dyn_cast<clang::NamedDecl>(decl)) {
        label += ' ';
        label += named->getNameAsString();
    }
    return label;
}

// --- \rSec parsing and the document-tree builder ----------------------------
//
// parse_rsec (producing SectionHeader) is declared in frontend.hpp and
// defined after this anonymous namespace closes, on the combinator library
// (decision parser-combinators) so its positioned failures are directly
// testable. parse_ref just below is a different marker (`\ref{...}`, unused
// by the \rSec grammar) and stays a hand scan.

// Recognize a `\ref{<stable>}` synopsis-group comment inside a class body
// (design §3.2) and return its stable name. The Beman form is
// `// \ref{stable.name}, human label`; only the stable name is needed here, to
// route an in-class-defined member's itemdescr to the matching `\rSec` section.
// Same tolerant, decoration-aware scan as parse_rsec; anything that is
// not a `\ref{...}` comment yields std::nullopt.
std::optional<std::string> parse_ref(std::string_view raw) {
    std::size_t pos     = 0;
    auto        skip_ws = [&] {
        pos = raw.find_first_not_of(" \t", pos);
        if (pos == std::string_view::npos)
            pos = raw.size();
    };

    skip_ws();
    if (raw.compare(pos, 3, "///") == 0 || raw.compare(pos, 3, "//!") == 0)
        pos += 3;
    else if (raw.compare(pos, 2, "//") == 0)
        pos += 2;
    else
        return std::nullopt;
    skip_ws();

    static constexpr std::string_view kTag = "\\ref";
    if (raw.compare(pos, kTag.size(), kTag) != 0)
        return std::nullopt;
    pos += kTag.size();

    skip_ws();
    if (pos >= raw.size() || raw[pos] != '{')
        return std::nullopt;
    ++pos;
    const std::size_t stable_begin = pos;
    const std::size_t stable_end   = raw.find('}', pos);
    if (stable_end == std::string_view::npos)
        return std::nullopt;
    return std::string(raw.substr(stable_begin, stable_end - stable_begin));
}

// One offset-ordered raw item: either a still-typed top-level decl or a
// comment's raw text, collected before classify() (the pipeline's first
// stage) converts it to a document_build::DocEvent. Mirrors SourceItem,
// but keeps the clang::Decl* (SourceItem only keeps a formatted label) so
// classify() can dispatch on it. Not to be confused with
// document_build::DocEvent, classify's clang-free output.
struct RawItem {
    unsigned     offset = 0;
    clang::Decl* decl   = nullptr; // non-null: a decl event
    std::string  comment_text;     // decl == nullptr: a comment event
};

using SkippedRanges = std::vector<std::pair<unsigned, unsigned>>;

SkippedRanges collect_skipped_ranges(clang::ASTUnit& ast) {
    SkippedRanges               result;
    const clang::SourceManager& sm        = ast.getSourceManager();
    const clang::FileID         main_file = sm.getMainFileID();
    clang::PreprocessingRecord* record    = ast.getPreprocessor().getPreprocessingRecord();
    if (record == nullptr)
        return result;

    // substrate generic algorithm: a filter-map whose bounds conversion needs
    // the SourceManager and can reject either endpoint after decomposition.
    for (const clang::SourceRange range : record->getSkippedRanges()) {
        if (!range.isValid())
            continue;
        const auto [begin_file, begin] = sm.getDecomposedLoc(range.getBegin());
        const clang::SourceLocation end_loc =
            clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm, ast.getLangOpts());
        const auto [end_file, end] = sm.getDecomposedLoc(end_loc);
        if (begin_file == main_file && end_file == main_file && end > begin)
            result.emplace_back(begin, end);
    }
    return result;
}

// --- normalization (design §3.6 step 2) -------------------------------------

// Draft-tuned FormatStyle (design §3.6 step 2): start from the LLVM style and
// layer on the options that make the draft's template/requires/concept
// conventions come out right — templates get their own line, requires-clauses
// go on their own (indented) line rather than trailing the declaration, and
// concepts break before their body. The column limit and return-type penalty
// favor the tighter, name-and-type-together shape draft synopses use. Per
// design §3.6, tuned via golden-file diff rather than derived a priori.
clang::format::FormatStyle draft_format_style() {
    clang::format::FormatStyle style     = clang::format::getLLVMStyle();
    style.BreakTemplateDeclarations      = clang::format::FormatStyle::BTDS_MultiLine;
    style.SpaceAfterTemplateKeyword      = false;
    style.RequiresClausePosition         = clang::format::FormatStyle::RCPS_OwnLine;
    style.IndentRequiresClause           = true;
    style.BreakBeforeConceptDeclarations = clang::format::FormatStyle::BBCDS_Always;
    style.ColumnLimit                    = 88;
    style.PenaltyReturnTypeOnItsOwnLine  = 1000; // keep return type and name together
    // WG21 draft wording attaches references/pointers to the type (`const T&`,
    // `T*`), not the declarator; getLLVMStyle() defaults to the opposite.
    style.PointerAlignment              = clang::format::FormatStyle::PAS_Left;
    style.KeepEmptyLines.AtStartOfBlock = false;
    return style;
}

// Run clang-format over `code` and apply its replacements, the same
// apply-or-fall-back shape smoke_check uses for its proof-of-life formatting
// call: if applyAllReplacements fails, consume the error and hand back the
// input unchanged rather than propagating a formatting failure into the IR.
std::string format_code(std::string_view code, const clang::format::FormatStyle& style) {
    const std::string                        text   = std::string(code);
    const std::vector<clang::tooling::Range> ranges = {clang::tooling::Range(0, static_cast<unsigned>(text.size()))};
    const clang::tooling::Replacements       replacements = clang::format::reformat(style, text, ranges);
    if (llvm::Expected<std::string> formatted = clang::tooling::applyAllReplacements(text, replacements))
        return *formatted;
    else
        llvm::consumeError(formatted.takeError());
    return text;
}

// --- reference-resolved namespace mapping (design §3.5) ---------------------
// "All rewrites act on identifier tokens whose AST referent is known — never
// text match." A qualifier is dropped only when its nested-name-specifier
// *resolves* to a namespace whose fully-qualified name is in the drop set:
// `std` (the draft writes library names unqualified, being inside namespace
// std) and the header's own namespace (`beman::optional`, which maps onto
// `std`). A `detail::` qualifier resolves to something not in the set and is
// left verbatim — the leakage checker is what flags those.

// The fully-qualified name an NNS writes, or nullopt when it is not a pure
// namespace qualifier (a type qualifier, `__super`, a dependent name).
std::optional<std::string> qualifier_namespace_name(clang::NestedNameSpecifier qualifier) {
    std::vector<std::string> parts;
    // substrate generic algorithm: each qualifier only yields its own prefix,
    // so the sequence of parts does not exist until this walk produces it --
    // an unfold over a linked structure, not a fold over a range that is
    // already there to iterate.
    while (qualifier.getKind() == clang::NestedNameSpecifier::Kind::Namespace) {
        const clang::NamespaceAndPrefix np    = qualifier.getAsNamespaceAndPrefix();
        const auto*                     named = llvm::dyn_cast_or_null<clang::NamedDecl>(np.Namespace);
        if (named == nullptr)
            return std::nullopt;
        parts.push_back(named->getNameAsString());
        qualifier = np.Prefix;
    }
    // Only a fully-resolved chain counts; anything rooted in a type or a
    // dependent specifier is left alone.
    if (qualifier.getKind() != clang::NestedNameSpecifier::Kind::Null &&
        qualifier.getKind() != clang::NestedNameSpecifier::Kind::Global)
        return std::nullopt;
    if (parts.empty())
        return std::nullopt;
    std::reverse(parts.begin(), parts.end());
    return parts | std::views::join_with(std::string_view("::")) | std::ranges::to<std::string>();
}

// The namespace that actually owns a referenced declaration. A using-
// declaration can make a std entity reachable through an implementation
// namespace, so this can intentionally differ from the qualifier written at
// the use site.
std::optional<std::string> declaration_namespace_name(const clang::NamedDecl* decl) {
    if (decl == nullptr)
        return std::nullopt;
    decl = decl->getUnderlyingDecl();
    if (const auto* specialization = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
        decl = specialization->getSpecializedTemplate();

    std::vector<std::string> parts;
    // substrate generic algorithm: unfold the declaration-context ancestry;
    // the next context is carried by the current node rather than an iterator.
    for (const clang::DeclContext* context = decl->getDeclContext(); context != nullptr;
         context                           = context->getParent()) {
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(context);
            ns != nullptr && !ns->isAnonymousNamespace())
            parts.push_back(ns->getNameAsString());
    }
    if (parts.empty())
        return std::nullopt;
    std::reverse(parts.begin(), parts.end());
    return parts | std::views::join_with(std::string_view("::")) | std::ranges::to<std::string>();
}

bool imported_qualifier_is_droppable(const clang::NamedDecl*       decl,
                                     clang::NestedNameSpecifierLoc qualifier,
                                     const std::set<std::string>&  drop) {
    if (decl == nullptr || !qualifier)
        return false;
    const std::optional<std::string> written = qualifier_namespace_name(qualifier.getNestedNameSpecifier());
    const std::optional<std::string> owner   = declaration_namespace_name(decl);
    return written && owner && !drop.contains(*written) && drop.contains(*owner);
}

// Collects the source ranges of droppable namespace qualifiers under a decl.
// RecursiveASTVisitor in this LLVM has no VisitNestedNameSpecifierLoc hook, so
// the traversal method itself is overridden.
class QualifierDropper : public clang::RecursiveASTVisitor<QualifierDropper> {
  public:
    QualifierDropper(const std::set<std::string>&                drop,
                     const clang::SourceManager&                 sm,
                     const clang::LangOptions&                   lang_opts,
                     std::vector<std::pair<unsigned, unsigned>>& out)
        : drop_(drop), sm_(sm), lang_opts_(lang_opts), out_(out) {}

    // RecursiveASTVisitor does not descend into a constructor's
    // explicit-specifier expression, so `explicit(std::is_convertible_v<U, T>)`
    // would keep its qualifier while the requires-clause beside it lost one.
    // Traverse it explicitly.
    bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl* ctor) {
        if (ctor != nullptr) {
            if (const clang::Expr* expr = ctor->getExplicitSpecifier().getExpr())
                TraverseStmt(const_cast<clang::Expr*>(expr));
        }
        return clang::RecursiveASTVisitor<QualifierDropper>::TraverseCXXConstructorDecl(ctor);
    }

    bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc nns) {
        if (nns) {
            const std::optional<std::string> name = qualifier_namespace_name(nns.getNestedNameSpecifier());
            if (name && drop_.count(*name) != 0) {
                const clang::SourceRange range = nns.getSourceRange();
                if (range.isValid() && range.getBegin().isFileID() && range.getEnd().isFileID()) {
                    const unsigned begin = sm_.getDecomposedLoc(range.getBegin()).second;
                    const unsigned end =
                        sm_.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm_, lang_opts_))
                            .second;
                    if (end > begin)
                        out_.emplace_back(begin, end);
                }
                return true; // the whole qualifier goes; do not descend into its prefix
            }
        }
        return clang::RecursiveASTVisitor<QualifierDropper>::TraverseNestedNameSpecifierLoc(nns);
    }

    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr)
            add_imported(expr->getDecl(), expr->getQualifierLoc());
        return true;
    }

    bool VisitConceptSpecializationExpr(clang::ConceptSpecializationExpr* expr) {
        if (expr != nullptr)
            add_imported(expr->getNamedConcept(), expr->getNestedNameSpecifierLoc());
        return true;
    }

    bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr == nullptr || !expr->getQualifierLoc())
            return true;
        const bool all_imported = expr->decls_begin() != expr->decls_end() &&
                                  std::ranges::all_of(expr->decls(), [&](clang::NamedDecl* decl) {
                                      return imported_qualifier_is_droppable(decl, expr->getQualifierLoc(), drop_);
                                  });
        if (all_imported)
            add_range(expr->getQualifierLoc());
        return true;
    }

  private:
    void add_imported(const clang::NamedDecl* decl, clang::NestedNameSpecifierLoc qualifier) {
        if (imported_qualifier_is_droppable(decl, qualifier, drop_))
            add_range(qualifier);
    }

    void add_range(clang::NestedNameSpecifierLoc qualifier) {
        const clang::SourceRange range = qualifier.getSourceRange();
        if (!range.isValid() || !range.getBegin().isFileID() || !range.getEnd().isFileID())
            return;
        const unsigned begin = sm_.getDecomposedLoc(range.getBegin()).second;
        const unsigned end =
            sm_.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm_, lang_opts_)).second;
        if (end > begin)
            out_.emplace_back(begin, end);
    }

    const std::set<std::string>&                drop_;
    const clang::SourceManager&                 sm_;
    const clang::LangOptions&                   lang_opts_;
    std::vector<std::pair<unsigned, unsigned>>& out_;
};

struct ExposUse {
    unsigned    name_begin;
    unsigned    name_end;
    unsigned    qualifier_begin;
    unsigned    qualifier_end;
    std::string display;
};

// Reference-resolved uses of namespace-scope exposition-only declarations.
// Each use carries its own qualifier range: marking detail::helper does not
// make the detail namespace globally visible or globally droppable.
class ExposUseFinder : public clang::RecursiveASTVisitor<ExposUseFinder> {
  public:
    ExposUseFinder(const std::map<const clang::Decl*, std::string>& expos,
                   const clang::SourceManager&                      sm,
                   const clang::LangOptions&                        lang_opts,
                   std::vector<ExposUse>&                           out)
        : expos_(expos), sm_(sm), lang_opts_(lang_opts), out_(out) {}

    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr)
            add(expr->getDecl(), expr->getNameInfo().getSourceRange(), expr->getQualifierLoc());
        return true;
    }

    bool VisitConceptSpecializationExpr(clang::ConceptSpecializationExpr* expr) {
        if (expr != nullptr)
            add(expr->getNamedConcept(),
                expr->getConceptNameInfo().getSourceRange(),
                expr->getNestedNameSpecifierLoc());
        return true;
    }

    bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr) {
            // A dependent variable-template-id retains lookup candidates
            // rather than becoming a DeclRefExpr until instantiation.
            for (clang::NamedDecl* candidate : expr->decls()) { // substrate generic algorithm
                add(candidate, expr->getNameInfo().getSourceRange(), expr->getQualifierLoc());
            }
        }
        return true;
    }

    // Type uses, for the exposition-only alias kinds: a plain alias is
    // written as a TypedefTypeLoc and an alias-template-id as a
    // TemplateSpecializationTypeLoc whose template name resolves to the
    // TypeAliasTemplateDecl — the same canonical decl the expos set keys.
    // Both carry their own qualifier loc on this LLVM, so a qualified
    // `detail::traverse_context_t<int>` drops its qualifier per use exactly
    // like the expression cases above.
    bool VisitTypedefTypeLoc(clang::TypedefTypeLoc tl) {
        add(tl.getDecl(), clang::SourceRange(tl.getNameLoc()), tl.getQualifierLoc());
        return true;
    }

    bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc tl) {
        add(tl.getTypePtr()->getTemplateName().getAsTemplateDecl(),
            clang::SourceRange(tl.getTemplateNameLoc()),
            tl.getQualifierLoc());
        return true;
    }

  private:
    void add(const clang::NamedDecl* decl, clang::SourceRange name_range, clang::NestedNameSpecifierLoc qualifier) {
        if (decl == nullptr)
            return;
        const clang::NamedDecl* marked_decl = decl;
        if (const auto* specialization = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
            marked_decl = specialization->getSpecializedTemplate();
        const auto found = expos_.find(marked_decl->getCanonicalDecl());
        if (found == expos_.end() || !name_range.isValid())
            return;

        const auto offset_after = [&](clang::SourceLocation loc) {
            return sm_.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(loc, 0, sm_, lang_opts_)).second;
        };
        const unsigned name_begin = sm_.getDecomposedLoc(name_range.getBegin()).second;
        const unsigned name_end   = offset_after(name_range.getEnd());
        unsigned       qual_begin = name_begin;
        unsigned       qual_end   = name_begin;
        if (qualifier) {
            const clang::SourceRange range = qualifier.getSourceRange();
            if (range.isValid()) {
                qual_begin = sm_.getDecomposedLoc(range.getBegin()).second;
                qual_end   = offset_after(range.getEnd());
            }
        }
        out_.push_back(ExposUse{name_begin, name_end, qual_begin, qual_end, found->second});
    }

    const std::map<const clang::Decl*, std::string>& expos_;
    const clang::SourceManager&                      sm_;
    const clang::LangOptions&                        lang_opts_;
    std::vector<ExposUse>&                           out_;
};

template <class AstNode>
std::vector<ExposUse> expos_uses(AstNode*                                         root,
                                 const std::map<const clang::Decl*, std::string>& expos,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts) {
    std::vector<ExposUse> out;
    if (root != nullptr && !expos.empty()) {
        ExposUseFinder finder(expos, sm, lang_opts, out);
        if constexpr (std::derived_from<AstNode, clang::Decl>)
            finder.TraverseDecl(root);
        else
            finder.TraverseStmt(root);
    }
    return out;
}

// Same, over a statement/expression subtree (a requires-clause condition, a
// static_assert condition, an extracted body).
std::vector<std::pair<unsigned, unsigned>> namespace_qualifier_edits(clang::Stmt*                 root,
                                                                     const std::set<std::string>& drop,
                                                                     const clang::SourceManager&  sm,
                                                                     const clang::LangOptions&    lang_opts) {
    std::vector<std::pair<unsigned, unsigned>> out;
    if (root != nullptr && !drop.empty()) {
        QualifierDropper dropper(drop, sm, lang_opts, out);
        dropper.TraverseStmt(root);
    }
    return out;
}

std::vector<std::pair<unsigned, unsigned>> namespace_qualifier_edits(clang::Decl*                 root,
                                                                     const std::set<std::string>& drop,
                                                                     const clang::SourceManager&  sm,
                                                                     const clang::LangOptions&    lang_opts) {
    std::vector<std::pair<unsigned, unsigned>> out;
    if (root != nullptr && !drop.empty()) {
        QualifierDropper dropper(drop, sm, lang_opts, out);
        dropper.TraverseDecl(root);
    }
    return out;
}

// --- subtractive synopsis extraction (design §3.4) --------------------------

// Which of the three comment vocabularies a line opens, read after leading
// horizontal whitespace.
//
// `//!` and `/*!` are **specgen's** markup: the docblock grammar parses them
// and design §3.4/§4 says to strip them from a synopsis. `///` and `/**` are
// **Doxygen's**, and specgen reads neither — a header written for Doxygen has
// prose where specgen's grammar expects an element tag, so parsing one
// produces a `prose before first element tag` error per block and an
// itemdescr made of API documentation. Everything else — a plain `//` or
// `/* */` — is **draft-form** text: a `\ref` group header, a `// see below`,
// a license block. That kind is kept verbatim.
//
// Doxygen's two spellings are nonetheless stripped from a synopsis, which is
// why this is a three-way classification and not a bool. The draft never
// prints implementation documentation, and a `///` line that merely stopped
// being markup would start *surviving* into the wording instead — a quieter
// version of the same defect.
enum class CommentVocabulary { Draft, Markup, Doxygen };

CommentVocabulary line_vocabulary(llvm::StringRef rest) {
    if (rest.starts_with("//!") || rest.starts_with("/*!"))
        return CommentVocabulary::Markup;
    if (rest.starts_with("///") || rest.starts_with("/**"))
        return CommentVocabulary::Doxygen;
    return CommentVocabulary::Draft;
}

// Byte offset within `raw` (a RawComment::getRawText() string, decoration
// included) of the first line whose vocabulary `accept` takes, or nullopt if
// no line does.
//
// Clang merges consecutive `//` line comments into a single RawComment, so the
// Beman shape
//
//     // \ref{optional.dtor}, destructor
//     //! \merge
//     constexpr ~optional() requires ... = default;
//
// arrives as ONE comment whose first line is a draft-form `\ref` header and
// whose markup starts partway in. Returning the offset lets callers keep the
// `\ref` line in the synopsis while still parsing (and stripping) the `//!`
// part — checking only the first line would miss the markup entirely.
template <typename Accept>
std::optional<std::size_t> vocabulary_start(llvm::StringRef raw, Accept accept) {
    std::size_t line_begin = 0;
    // substrate generic algorithm: the function's own contract is a byte
    // offset into raw, so each line's starting offset must survive scanning
    // it -- views::split('\n') would hand back substrings and throw that
    // offset away, forcing the same bookkeeping back in through a side
    // channel.
    while (line_begin <= raw.size()) {
        std::size_t pos = raw.find_first_not_of(" \t", line_begin);
        if (pos == llvm::StringRef::npos)
            pos = raw.size();
        if (accept(line_vocabulary(raw.substr(pos))))
            return line_begin;
        const std::size_t newline = raw.find('\n', line_begin);
        if (newline == llvm::StringRef::npos)
            break;
        line_begin = newline + 1;
    }
    return std::nullopt;
}

// Byte offset within `raw` of the first *docblock* line — specgen markup only
// — or nullopt if the comment carries no markup at all. What the grammar is
// pointed at.
std::optional<std::size_t> docblock_start(llvm::StringRef raw) {
    return vocabulary_start(raw, [](CommentVocabulary v) { return v == CommentVocabulary::Markup; });
}

// Byte offset within `raw` of the first line a synopsis must not show —
// specgen markup *or* Doxygen. What extract_synopsis cuts from.
std::optional<std::size_t> stripped_comment_start(llvm::StringRef raw) {
    return vocabulary_start(raw, [](CommentVocabulary v) { return v != CommentVocabulary::Draft; });
}

// Does `raw` carry markup anywhere (see docblock_start)?
bool is_docblock_comment(llvm::StringRef raw) { return docblock_start(raw).has_value(); }

// Translate a decl's docblock findings into document_build::Diagnostics
// (design §9's "diagnostics are golden text"): same severity, same
// message, with the docblock-relative line turned into a main-file line so a
// reader can go to it.
//
// The arithmetic is addition, not a lookup, and that is a property of how the
// text got here: the caller hands parse_docblock a *contiguous slice* of the
// file (the raw comment from `docblock_start` on), and
// strip_comment_decorations rewrites each line in place without adding or
// removing any, so docblock line N is main-file line (line of the slice's
// first character) + N - 1. A
// diagnostic with line 0 is one of the whole-block cross-checks (`\effects`
// and `\effects-equiv` are mutually exclusive, and its `\returns` twin), which
// belong to no single line; they report against the docblock's first line
// rather than against line 0 of the file, which does not exist.
std::vector<beman::specgen::document_build::Diagnostic>
docblock_diagnostics(const clang::RawComment*                rc,
                     std::size_t                             markup_start,
                     const std::vector<grammar::Diagnostic>& diags,
                     const clang::SourceManager&             sm) {
    if (diags.empty())
        return {};
    const unsigned first_line =
        sm.getSpellingLineNumber(rc->getBeginLoc().getLocWithOffset(static_cast<int>(markup_start)));
    return diags | std::views::transform([&](const grammar::Diagnostic& d) {
               const unsigned line = d.line > 0 ? first_line + static_cast<unsigned>(d.line) - 1 : first_line;
               return beman::specgen::document_build::Diagnostic{d.severity, line, d.message};
           }) |
           std::ranges::to<std::vector<beman::specgen::document_build::Diagnostic>>();
}

// Does `decl` carry a `//!`/`/*!` docblock of its own? The two-step the
// attach path and the roster both need before trusting a raw comment:
// getRawCommentForDeclNoCache attaches whatever comment immediately precedes
// the decl, which may well be a draft-form `\ref`/`\rSec` line that belongs to
// the synopsis rather than markup for this entity.
bool has_docblock(const clang::Decl* decl, const clang::SourceManager& sm) {
    const clang::RawComment* rc = decl->getASTContext().getRawCommentForDeclNoCache(decl);
    return rc != nullptr && is_docblock_comment(rc->getRawText(sm));
}

// Lower just the directives (markers) from a decl's `//!` docblock, or a
// default-constructed set if it has none. Used by the omit-set and
// expos-set pre-passes to read `\omit`/`\merge`/`\expos` without building a full
// itemdescr; attach_function lowers the descr and directives together on the
// attach path. Takes any Decl so it serves data members (`\expos`) as well as
// functions.
beman::specgen::lowering::ItemDirectives docblock_directives(const clang::Decl* decl, const clang::SourceManager& sm) {
    if (const clang::RawComment* rc = decl->getASTContext().getRawCommentForDeclNoCache(decl)) {
        const llvm::StringRef raw = rc->getRawText(sm);
        if (const std::optional<std::size_t> start = docblock_start(raw))
            return beman::specgen::lowering::lower(
                       beman::specgen::grammar::parse_docblock(raw.substr(*start).str()).block)
                .directives;
    }
    return {};
}

// A top-level record marked \omit/\merge contributes no synopsis or derived
// class wording. Return an engaged diagnostics vector only for that case, so
// classify() can distinguish an unmarked record from a cleanly marked one
// while preserving every finding from the marker's own docblock.
std::optional<std::vector<beman::specgen::document_build::Diagnostic>>
record_suppression_diagnostics(const clang::Decl* decl, const clang::SourceManager& sm) {
    const clang::RawComment* rc = decl->getASTContext().getRawCommentForDeclNoCache(decl);
    if (rc == nullptr)
        return std::nullopt;
    const llvm::StringRef            raw   = rc->getRawText(sm);
    const std::optional<std::size_t> start = docblock_start(raw);
    if (!start)
        return std::nullopt;

    const grammar::ParseResult parsed  = grammar::parse_docblock(raw.substr(*start).str());
    const lowering::Lowered    lowered = lowering::lower(parsed.block);
    if (!lowered.directives.omit && !lowered.directives.merge)
        return std::nullopt;
    return docblock_diagnostics(rc, *start, parsed.diags, sm);
}

// One text edit against the extracted synopsis substring, in absolute
// main-file byte offsets (translated to offsets relative to the class span
// just before being applied — see extract_synopsis).
struct SynopsisEdit {
    unsigned    begin = 0;
    unsigned    end   = 0;
    std::string replacement;
};

enum class SeeBelowTarget { ReturnType, Noexcept, Explicit };
using SeeBelowMap = std::map<const clang::Decl*, SeeBelowTarget>;

enum class FreestandingKind { Freestanding, Deleted };
using FreestandingMap = std::map<const clang::Decl*, FreestandingKind>;

enum class AliasMask { SeeBelow, ImplDefined };

std::optional<AliasMask> alias_mask(const grammar::Markers& markers) {
    if (markers.impdef)
        return AliasMask::ImplDefined;
    if (markers.seebelow && !markers.seebelow_target)
        return AliasMask::SeeBelow;
    return std::nullopt;
}

std::optional<clang::SourceRange> alias_rhs_source_range(const clang::TypeAliasDecl* alias,
                                                         const clang::SourceManager& sm,
                                                         const clang::LangOptions&   lang_opts) {
    const clang::TypeSourceInfo* source = alias->getTypeSourceInfo();
    if (source == nullptr)
        return std::nullopt;
    clang::SourceRange range = source->getTypeLoc().getSourceRange();
    // QualifiedTypeLoc begins at its unqualified child on this Clang, so a
    // written leading `const` is not necessarily in getSourceRange(). Anchor
    // the end in TypeLoc, then recover the first RHS token from the `=` that
    // follows the alias identifier.
    const std::optional<clang::Token> equal = clang::Lexer::findNextToken(alias->getLocation(), sm, lang_opts);
    if (equal && equal->is(clang::tok::equal)) {
        const std::optional<clang::Token> first = clang::Lexer::findNextToken(equal->getLocation(), sm, lang_opts);
        if (first)
            range.setBegin(first->getLocation());
    }
    return range.isValid() ? std::optional{range} : std::nullopt;
}

std::optional<SeeBelowTarget> seebelow_target(const grammar::Markers& markers) {
    if (!markers.seebelow)
        return std::nullopt;
    if (!markers.seebelow_target)
        return SeeBelowTarget::ReturnType;
    if (*markers.seebelow_target == "noexcept")
        return SeeBelowTarget::Noexcept;
    if (*markers.seebelow_target == "explicit")
        return SeeBelowTarget::Explicit;
    return std::nullopt; // parse_docblock has already diagnosed the unknown target
}

std::optional<clang::SourceRange> seebelow_source_range(const clang::FunctionDecl* fn, SeeBelowTarget target) {
    const clang::SourceRange valid = [&]() -> clang::SourceRange {
        switch (target) {
        case SeeBelowTarget::ReturnType:
            return fn->getReturnTypeSourceRange();
        case SeeBelowTarget::Noexcept:
            if (const auto* type = fn->getType()->getAs<clang::FunctionProtoType>())
                if (const clang::Expr* expr = type->getNoexceptExpr())
                    return expr->getSourceRange();
            return {};
        case SeeBelowTarget::Explicit:
            if (const clang::Expr* expr = clang::ExplicitSpecifier::getFromDecl(fn).getExpr())
                return expr->getSourceRange();
            return {};
        }
        std::unreachable();
    }();
    return valid.isValid() ? std::optional{valid} : std::nullopt;
}

bool edits_overlap(const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin < b.end && b.begin < a.end; }

// A see-below replacement masks the expression's implementation spelling as
// a whole. Namespace/exposition rewrites nested inside it must therefore lose
// to the enclosing edit rather than causing the descending-offset watermark
// to skip the see-below edit after applying an inner one.
void add_dominant_edit(std::vector<SynopsisEdit>& edits, SynopsisEdit dominant) {
    std::erase_if(edits, [&](const SynopsisEdit& edit) { return edits_overlap(edit, dominant); });
    edits.push_back(std::move(dominant));
}

// --- token rewriting via valid-C++ sentinels (design §3.6) ------------------
// A rewrite replaces a token range with a `\exposid`/`\seebelow`/… span. Doing
// it directly would put backend markup (`@\exposid{…}@`) into text clang-format
// then mangles, so §3.6 splits it in three: (1) replace the range with a unique
// valid-C++ *sentinel* identifier, (2) format, (3) recover — swap each sentinel
// for its display text and record an ir::Span over it. This keeps the fragment
// parseable and lets clang-format line-break on a real token.

// The recovered form of one sentinel: `display` is what lands in CodeText::text
// (e.g. the exposid name `val`); `payload` drives the backend macro (the exposid
// name, or empty for \seebelow, whose macro takes no argument).
struct SpanInfo {
    beman::specgen::ir::SpanKind kind;
    std::string                  display;
    std::string                  payload;
};

// A unique, valid-C++ sentinel identifier for the n-th span in a fragment.
std::string span_sentinel(unsigned n) { return std::format("SPECGEN_{}_SPAN", n); }

// §3.6 step 3: replace each sentinel with its display text and record an
// ir::Span over it. Kept separate from formatting because derived conjuncts
// are expression fragments whose original layout is already the desired text.
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
    // primitive backend/common.hpp's render_code_spans already marks -- not a
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

beman::specgen::ir::CodeText format_and_recover(std::string                            text,
                                                const std::map<std::string, SpanInfo>& sentinels,
                                                std::optional<std::string_view>        record_tag = std::nullopt) {
    std::string formatted = format_code(text, draft_format_style());

    // BTDS_MultiLine matches the draft for function templates, but considers a
    // record head short even when its body is not and joins `template<...>` to
    // `class`/`struct`. The AST gives this path the record tag, so preserve
    // that one draft line break without guessing at arbitrary formatted code.
    if (record_tag && !record_tag->empty() && formatted.starts_with("template<")) {
        const std::size_t line_end = formatted.find('\n');
        const std::size_t pos      = formatted.rfind(std::format(" {} ", *record_tag), line_end);
        if (pos != std::string::npos && pos < line_end)
            formatted[pos] = '\n';
    }
    return recover_sentinels(std::move(formatted), sentinels);
}

// --- exposition-only *uses* (design §3.5) -----------------------------------
// §3.5's stated exception to reference-resolved rewriting: "dependent member
// uses of the class's own expos members may fall back to name match within the
// class's fragments only". That is exactly this — the spellings are the class's
// own expos members, and the scan is confined to fragments extracted from that
// class (its equiv bodies and its prose), never the whole file.

// The enclosing class of a function: the semantic parent for a member, the
// lexical one for a hidden friend (semantically a namespace member).
const clang::CXXRecordDecl* enclosing_record(const clang::FunctionDecl* fn) {
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(fn))
        return method->getParent();
    return llvm::dyn_cast_or_null<clang::CXXRecordDecl>(fn->getLexicalDeclContext());
}

struct RealRecordMember {
    const clang::Decl* decl;
    bool               effectively_private;
    bool               nested_anonymous;
};

// Flatten only anonymous structs/unions. Clang represents an anonymous union
// with a nested record holding the source fields plus implicit FieldDecl and
// IndirectFieldDecl projections on the enclosing class. The projections have
// no source declaration of their own; walking the nested record gives each
// real member exactly once and leaves named nested classes as class members.
void append_real_record_members(const clang::CXXRecordDecl*    record,
                                bool                           enclosed_private,
                                bool                           nested_anonymous,
                                std::vector<RealRecordMember>& out) {
    // substrate generic algorithm: recursive tree flattening with inherited
    // access and provenance state; only anonymous record nodes are expanded.
    for (const clang::Decl* member : record->decls()) {
        if (member->isImplicit() || llvm::isa<clang::IndirectFieldDecl>(member))
            continue;

        const bool effectively_private = enclosed_private || member->getAccess() == clang::AS_private;
        if (const auto* nested = llvm::dyn_cast<clang::CXXRecordDecl>(member);
            nested != nullptr && nested->isAnonymousStructOrUnion()) {
            append_real_record_members(nested, effectively_private, true, out);
            continue;
        }
        out.push_back(RealRecordMember{member, effectively_private, nested_anonymous});
    }
}

std::vector<RealRecordMember> real_record_members(const clang::CXXRecordDecl* record, bool enclosed_private = false) {
    std::vector<RealRecordMember> members;
    append_real_record_members(record, enclosed_private, false, members);
    return members;
}

const clang::FunctionDecl* member_function_or_template(const clang::Decl* member);

// `record`'s exposition-only members as written-spelling → exposid display name.
std::map<std::string, std::string> expos_spellings(const clang::CXXRecordDecl*                      record,
                                                   const std::map<const clang::Decl*, std::string>& expos_set) {
    if (record == nullptr)
        return {};
    auto expos_members = real_record_members(record) | std::views::filter([&](const RealRecordMember& member) {
                             const auto* member_fn = member_function_or_template(member.decl);
                             const auto* named = member_fn != nullptr ? llvm::dyn_cast<clang::NamedDecl>(member_fn)
                                                                      : llvm::dyn_cast<clang::NamedDecl>(member.decl);
                             return named != nullptr && named->getIdentifier() != nullptr &&
                                    expos_set.contains(named->getCanonicalDecl());
                         });

    std::map<std::string, std::string> spellings;
    // substrate generic algorithm: a scatter into a map keyed by data, the
    // same shape document_build.cpp's pending-bucket population is marked
    // for. Not `ranges::to<map>`: two overloads in one class can share a
    // written spelling, and assignment keeps the *last* of them, whereas
    // `ranges::to` selects map's from_range_t (insert) constructor and would
    // silently keep the first instead.
    for (const RealRecordMember& member : expos_members) {
        const auto* member_fn = member_function_or_template(member.decl);
        const auto* named =
            member_fn != nullptr ? llvm::cast<clang::NamedDecl>(member_fn) : llvm::cast<clang::NamedDecl>(member.decl);
        spellings[named->getName().str()] = expos_set.at(named->getCanonicalDecl());
    }
    return spellings;
}

bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Replace whole-identifier occurrences of an expos spelling with a sentinel, so
// format_and_recover turns each into an `\exposid` span. Whole-word only: a
// member named `val` must not rewrite the `val` inside `evaluate`.
std::string apply_expos_sentinels(std::string                               text,
                                  const std::map<std::string, std::string>& spellings,
                                  unsigned&                                 span_n,
                                  std::map<std::string, SpanInfo>&          sentinels) {
    if (spellings.empty())
        return text;
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk
    // that rewrites whole-identifier matches into sentinels is the
    // array-substitution primitive itself (the same shape backend/common.hpp's
    // render_code_spans marks) -- there is no smaller unit to fold over.
    while (pos < text.size()) {
        if (!is_ident_char(text[pos]) || (pos > 0 && is_ident_char(text[pos - 1]))) {
            out += text[pos++];
            continue;
        }
        const std::size_t end = static_cast<std::size_t>(
            std::ranges::find_if_not(text.cbegin() + static_cast<std::ptrdiff_t>(pos), text.cend(), is_ident_char) -
            text.cbegin());
        const std::string word = text.substr(pos, end - pos);
        if (const auto it = spellings.find(word); it != spellings.end()) {
            const std::string sentinel = span_sentinel(span_n++);
            sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, it->second, it->second};
            out += sentinel;
        } else {
            out += word;
        }
        pos = end;
    }
    return out;
}

// Design §7: a `\ref{stable.name}` in a synopsis group comment is a
// *cross-reference*, not code text, so it becomes an `ir::SpanKind::Ref` span
// here. Keeping the draft's own `// \ref{optional.ctor},
// constructors` verbatim would leave a per-backend escape sitting in the IR,
// which design §7 forbids in as many words — LaTeX
// passing through a LaTeX backend is indistinguishable from correct output,
// while markdown would show a LaTeX macro in a ```cpp fence.
//
// Restricted to the tail of a `//` comment, which is both where the draft
// writes one and what the enumerator's own documentation says it is. Scanning
// the whole fragment would be shorter and would *invent* a span for a `\ref{`
// inside a string literal -- the wrong direction for imprecision to run, by
// the leakage checker's own discipline. Two known gaps, neither reached
// by any corpus header: a `\ref` in a `/* */` comment is not converted (the
// only `/*!` form a synopsis sees is a docblock, stripped before this runs),
// and one split across a line break is left alone.
std::string apply_ref_sentinels(std::string text, unsigned& span_n, std::map<std::string, SpanInfo>& sentinels) {
    namespace ir                    = beman::specgen::ir;
    constexpr std::string_view kRef = "\\ref{";

    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    // substrate generic algorithm: a position-tracking left-to-right walk that
    // rewrites matches into sentinels is the array-substitution primitive
    // itself -- the same shape apply_expos_sentinels above is marked for.
    while (true) {
        const std::size_t at = text.find(kRef, pos);
        if (at == std::string::npos)
            break;
        const std::size_t newline    = text.rfind('\n', at);
        const std::size_t line_begin = newline == std::string::npos ? 0 : newline + 1;
        const std::size_t line_end   = std::min(text.find('\n', at), text.size());
        const std::size_t comment    = text.find("//", line_begin);
        const std::size_t close      = text.find('}', at + kRef.size());
        if (comment == std::string::npos || comment > at || close == std::string::npos || close > line_end) {
            // Not a cross-reference we can name: leave the text alone and
            // resume past this occurrence rather than rescanning it.
            out += text.substr(pos, at + kRef.size() - pos);
            pos = at + kRef.size();
            continue;
        }

        const std::string name     = text.substr(at + kRef.size(), close - at - kRef.size());
        const std::string sentinel = span_sentinel(span_n++);
        // The display text is what the draft *renders* -- `\ref{x}` sets
        // "[x]" -- so a reader of CodeText::text alone, and any backend that
        // ignored spans, still sees a cross-reference rather than a macro.
        sentinels[sentinel] = SpanInfo{ir::SpanKind::Ref, "[" + name + "]", name};
        out += text.substr(pos, at - pos);
        out += sentinel;
        pos = close + 1;
    }
    out += text.substr(pos);
    return out;
}

// Rewrite expos spellings inside a description's inline code spans (design §4.1:
// "backticked spans are reference-resolved: expos names render as \exposid").
// A `` `value_` `` in prose becomes an ExposId span carrying the exposid name,
// so the backend emits \exposid{value} rather than \tcode{value_}.
void rewrite_prose_expos(beman::specgen::ir::ItemDescr& descr, const std::map<std::string, std::string>& spellings) {
    namespace ir = beman::specgen::ir;
    if (spellings.empty())
        return;
    // Each level rebuilds a fresh container via transform rather than
    // mutating the one it was handed -- a write at the current position is an
    // append. `ir::Inline`/`ir::Paragraph`/`ir::DescriptionElement` are all
    // cheap to move (variant-of-strings, vector-of-that), so the rebuild
    // costs nothing a reference mutation wouldn't already pay for the string
    // copies inside it.
    const auto fix_inline = [&](ir::Inline piece) {
        auto* code = std::get_if<ir::CodeInline>(&piece);
        if (code == nullptr || !code->code.spans.empty())
            return piece;
        const auto it = spellings.find(code->code.text);
        if (it == spellings.end())
            return piece;
        code->code.text  = it->second;
        code->code.spans = {ir::Span{0, code->code.text.size(), ir::SpanKind::ExposId, it->second}};
        return piece;
    };
    const auto fix_paragraph = [&](ir::Paragraph para) {
        return std::move(para) | std::views::as_rvalue | std::views::transform(fix_inline) |
               std::ranges::to<ir::Paragraph>();
    };
    // Shared by both arms below (prose paragraphs and itemize items) so the
    // walk over a `vector<Paragraph>` exists exactly once.
    const auto fix_paragraphs = [&](std::vector<ir::Paragraph> paras) {
        return std::move(paras) | std::views::as_rvalue | std::views::transform(fix_paragraph) |
               std::ranges::to<std::vector<ir::Paragraph>>();
    };
    const auto fix_element = [&](ir::DescriptionElement el) {
        el.paragraphs = fix_paragraphs(std::move(el.paragraphs));
        if (el.itemize)
            el.itemize->items = fix_paragraphs(std::move(el.itemize->items));
        if (el.table) {
            el.table->caption = fix_paragraph(std::move(el.table->caption));
            el.table->column1 = fix_paragraph(std::move(el.table->column1));
            el.table->column2 = fix_paragraph(std::move(el.table->column2));
            el.table->rows    = std::move(el.table->rows) | std::views::as_rvalue |
                                std::views::transform([&](ir::Table2DRow row) {
                                 row.header = fix_paragraph(std::move(row.header));
                                 row.cell1  = fix_paragraph(std::move(row.cell1));
                                 row.cell2  = fix_paragraph(std::move(row.cell2));
                                 return row;
                                }) |
                                std::ranges::to<std::vector<ir::Table2DRow>>();
        }
        return el;
    };
    descr.elements = std::move(descr.elements) | std::views::as_rvalue | std::views::transform(fix_element) |
                     std::ranges::to<std::vector<ir::DescriptionElement>>();
}

// The function a class-body member declares, for the passes that must see
// *every* member function the way the synopsis does: a plain method, a member
// function template (a FunctionTemplateDecl in the member list, unwrapped so
// the canonical-decl-keyed marker sets match it), a hidden friend, or
// a hidden friend template. Null for anything else (a field, a nested type, an
// access label).
//
// This is the widest member projection: synopsis extraction, roster building,
// in-class attachment, and marker pre-passes use it when member templates must
// behave like plain members.
const clang::FunctionDecl* member_function_or_template(const clang::Decl* member) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(member))
        return fn;
    if (const auto* fn_tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(member))
        return fn_tmpl->getTemplatedDecl();
    if (const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member)) {
        if (const auto* fn = llvm::dyn_cast_or_null<clang::FunctionDecl>(friend_decl->getFriendDecl()))
            return fn;
        if (const auto* ft = llvm::dyn_cast_or_null<clang::FunctionTemplateDecl>(friend_decl->getFriendDecl()))
            return ft->getTemplatedDecl();
    }
    return nullptr;
}

// Subtractive synopsis extraction (design §3.4): lex the class's own text out
// of the main file, then remove/splice exactly two things — nothing else —
// and (design §3.6 step 2) reformat the result with the draft
// FormatStyle. Token rewriting (§3.5, §3.6 step 1) is applied through the
// sentinel edits accumulated below.
//
// `record` supplies the member list to walk (bodies to splice, docblocks to
// strip) and, absent `head_decl`, the extraction range's begin location too.
// For an ordinary class that is the same decl either way. For a class
// template, `record` is the *templated* CXXRecordDecl (members still hang off
// it) but the synopsis has to start at the `template` keyword, which belongs
// to the enclosing ClassTemplateDecl — pass that as `head_decl` so the
// extraction range covers `template <...> ... class ... { ... };` whole.
//
// `omit_set` holds the canonical declarations of members to remove from
// the synopsis entirely — `\omit`ted decls and `\merge`d twins (design §4.3) —
// keyed on getCanonicalDecl() so an out-of-line marker matches the in-class
// declaration walked here.
//
// `expos_set` maps a member's canonical declaration to its
// exposition-only display name (design §4.3, §3.5): such a member renders with
// its declared name replaced by an `\exposid` span and a trailing
// `// exposition only` comment.
beman::specgen::ir::CodeText extract_synopsis(const clang::CXXRecordDecl*                      record,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              const std::set<const clang::Decl*>&              omit_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set,
                                              const SeeBelowMap&                               seebelow_map,
                                              const FreestandingMap&                           freestanding_map,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const clang::Decl*                               head_decl = nullptr) {
    const llvm::StringRef buffer = sm.getBufferData(sm.getMainFileID());

    const clang::Decl* head = head_decl != nullptr ? head_decl : record;

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
    const auto add_library_index = [&](const clang::NamedDecl* named, std::string parent) {
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

    add_library_index(record, {});

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
        record->getASTContext().Comments.getCommentsInFile(sm.getMainFileID());
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
    std::vector<std::pair<unsigned, unsigned>> removed_ranges; // whole-member removals
    const auto                                 omit_line = [&](const clang::Decl* outer) {
        const unsigned remove_begin = line_start(sm.getDecomposedLoc(outer->getBeginLoc()).second);
        unsigned       remove_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(outer->getEndLoc(), 0, sm, lang_opts)).second;
        if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(outer->getEndLoc(), sm, lang_opts);
            semi && semi->is(clang::tok::semi))
            remove_end = sm.getDecomposedLoc(semi->getLocation()).second + 1;
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
    struct AccessLabel {
        const clang::Decl*     decl;
        clang::AccessSpecifier access;
        bool                   survivor = false;
    };
    std::vector<AccessLabel>   labels;
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
            labels.push_back(AccessLabel{member, access->getAccess()});
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

        if (const auto* field = llvm::dyn_cast<clang::FieldDecl>(member)) {
            if (const auto it = expos_set.find(field->getCanonicalDecl()); it != expos_set.end())
                add_exposid(field, it->second);
            mark_survivor(); // a shown (public or expos) data member
            continue;
        }

        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(member)) {
            // A documented alias gets a standalone member index on its
            // itemdecl. An unmarked alias is synopsis-only, so index its name
            // here instead. Exposition names already occupy this byte range
            // with an ExposId span and cannot be nested in CodeText.
            if (!has_docblock(alias, sm) && !expos_set.contains(alias->getCanonicalDecl()))
                add_library_index(alias, record->getNameAsString());
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
                unsigned semi_end =
                    sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(fn->getEndLoc(), 0, sm, lang_opts)).second;
                if (const std::optional<clang::Token> semi =
                        clang::Lexer::findNextToken(fn->getEndLoc(), sm, lang_opts);
                    semi && semi->is(clang::tok::semi))
                    semi_end = sm.getDecomposedLoc(semi->getLocation()).second + 1;
                edits.push_back(SynopsisEdit{semi_end, semi_end, std::string(freestanding_comment)});
            }
            continue;
        }
        const clang::Stmt* body = fn->getBody();
        if (body == nullptr)
            continue;

        const unsigned              body_begin = sm.getDecomposedLoc(body->getBeginLoc()).second;
        const clang::SourceLocation body_end_tok =
            clang::Lexer::getLocForEndOfToken(body->getEndLoc(), 0, sm, lang_opts);
        const unsigned body_end = sm.getDecomposedLoc(body_end_tok).second;
        edits.push_back(SynopsisEdit{body_begin, body_end, std::format(";{}", freestanding_comment)});
    }
    open_groups_before(class_end);
    finish_group();

    // Drop `private:`/`protected:` labels whose entire section was omitted.
    const auto orphaned_label = [](const AccessLabel& label) {
        return !label.survivor && (label.access == clang::AS_private || label.access == clang::AS_protected);
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
    // that was removed wholesale is already gone with it — emitting it too
    // would be an edit nested inside another.
    const auto inside_removed = [&removed_ranges](unsigned begin, unsigned end) {
        return std::ranges::any_of(removed_ranges, [&](const auto& r) { return begin >= r.first && end <= r.second; });
    };
    edits.append_range(
        namespace_qualifier_edits(const_cast<clang::CXXRecordDecl*>(record), ns_drop_set, sm, lang_opts) |
        std::views::filter([&](const auto& p) {
            return p.first >= class_begin && p.second <= class_end && !inside_removed(p.first, p.second);
        }) |
        std::views::transform([](const auto& p) { return SynopsisEdit{p.first, p.second, ""}; }));

    // substrate generic algorithm: each use allocates a unique sentinel while
    // conditionally emitting its qualifier deletion, so this is a stateful
    // flat-map into two coupled outputs rather than a transform.
    for (const ExposUse& use : expos_uses(const_cast<clang::CXXRecordDecl*>(record), expos_set, sm, lang_opts)) {
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
                 return kv.first >= class_begin && kv.first < class_end;
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
    std::sort(
        edits.begin(), edits.end(), [](const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin > b.begin; });
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
    return format_and_recover(std::move(text), sentinels, std::string_view(record_tag.data(), record_tag.size()));
}

// One free-standing namespace-scope declaration, extracted whole: the
// template head and initializer/constraint through the trailing semicolon,
// with the same qualifier drops and expos-use sentinels as class synopses.
// Two callers share it. The `\expos` standalone synopsis
// (extract_namespace_expos_synopsis below) passes `exposition` to rewrite the
// declared name as an `\exposid` span and append the draft's exposition-only
// comment. A documented record declaration the header never defines (an
// undefined class-template primary — classify_record_declaration below)
// passes `record_tag` instead, keeping the draft's template-head line break
// the same way extract_synopsis does, and takes the text verbatim: the
// declaration *is* the wording, an itemdecl rather than a Synopsis.
beman::specgen::ir::CodeText
extract_freestanding_declaration(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                 bool                                             exposition,
                                 std::optional<std::string_view>                  record_tag = std::nullopt) {
    const clang::SourceLocation begin_loc  = named->getBeginLoc();
    const unsigned              decl_begin = sm.getDecomposedLoc(begin_loc).second;

    clang::SourceLocation end_loc = clang::Lexer::getLocForEndOfToken(named->getEndLoc(), 0, sm, lang_opts);
    if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(named->getEndLoc(), sm, lang_opts);
        semi && semi->is(clang::tok::semi))
        end_loc = clang::Lexer::getLocForEndOfToken(semi->getLocation(), 0, sm, lang_opts);
    const unsigned decl_end = sm.getDecomposedLoc(end_loc).second;

    const llvm::StringRef buffer = sm.getBufferData(sm.getMainFileID());
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
        namespace_qualifier_edits(const_cast<clang::NamedDecl*>(named), ns_drop_set, sm, lang_opts) |
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

    std::sort(
        edits.begin(), edits.end(), [](const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin > b.begin; });
    unsigned applied_begin = decl_end + 1;
    // substrate generic algorithm: the same descending-offset scatter and
    // overlap watermark as class synopsis and itemdecl extraction.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        text.replace(edit.begin - decl_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return format_and_recover(std::move(text), sentinels, record_tag);
}

// A free-standing exposition-only entity is already a complete
// definition, so it uses the ordinary Synopsis IR rather than inventing an
// itemdecl or a fragment-shaped node. Keep the declaration's template head and
// initializer/constraint through its semicolon, rewrite the declared name and
// any references to other exposition-only entities with the same sentinels as
// class synopses, and append the draft's exposition-only comment.
beman::specgen::ir::CodeText
extract_namespace_expos_synopsis(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set) {
    return extract_freestanding_declaration(named, sm, lang_opts, ns_drop_set, expos_set, /*exposition=*/true);
}

// --- redeclaration-chain attachment (design §3.3) ---------------------------

// Itemdecl text renders from the in-class declaration, never the out-of-line
// definition (design §3.3): the out-of-line form carries `Class::`
// qualification, `inline`, and stacked template heads the draft never shows.
// `in_class` is ordinarily declaration-only for this tier's corpus (no body
// to splice), but defensively splice one to `;` the same way extract_synopsis
// does if it ever shows up, rather than emitting a definition as an itemdecl.
//
// `strip_requires_clause` is the default (design §5.1: "requires-clause
// is removed from the itemdecl"): when `in_class` carries a trailing
// requires-clause, truncate the text at its own `getBeginLoc()` (the in-class
// decl's clause, since the itemdecl text is the in-class decl's own text —
// though per C++ it must match the out-of-line definition's), right-trim, and
// drop a trailing bare `requires` token left behind by the cut. Callers pass
// false for a `\constraints-in-decl` declaration, which keeps the clause
// verbatim instead of deriving a Constraints element from it.
//
// `friend_begin` is the enclosing FriendDecl's begin location for a
// hidden friend: the `friend` keyword belongs to the FriendDecl, not the inner
// FunctionDecl (whose range starts at the return type), so extracting from it
// keeps `friend` in the itemdecl (the draft convention — cf. [expected.object.eq]).
// Invalid (the default) for an ordinary member, whose range begin is used as-is.
//
// `seebelow` (design §4.3) replaces the declared return type, or
// the operand of a conditional noexcept/explicit specifier, with a `\seebelow`
// span. The keyword and parentheses around a named operand remain authored.
beman::specgen::ir::CodeText extract_itemdecl(const clang::FunctionDecl*                       in_class,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              bool                                             strip_requires_clause,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set,
                                              clang::SourceLocation                            friend_begin = {},
                                              std::optional<SeeBelowTarget> seebelow = std::nullopt) {
    // A member template's own `template <...>` head is part of its itemdecl
    // (the draft shows it), but a bare FunctionDecl's range starts at the
    // return type/name — the head lives on the described FunctionTemplateDecl.
    // Extract from that when present so the head is kept. (The enclosing class
    // template's head is not on this decl, so it is correctly excluded.)
    const clang::Decl* range_decl = in_class;
    if (const clang::FunctionTemplateDecl* described = in_class->getDescribedFunctionTemplate())
        range_decl = described;

    const clang::SourceLocation begin_loc   = friend_begin.isValid() ? friend_begin : range_decl->getBeginLoc();
    const unsigned              decl_begin  = sm.getDecomposedLoc(begin_loc).second;
    const llvm::StringRef       source_text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(begin_loc, range_decl->getEndLoc()), sm, lang_opts);

    std::string text;
    // As in extract_synopsis, an ODR-used explicitly defaulted member
    // can carry a synthesized body whose source range is not a real body.
    if (in_class->doesThisDeclarationHaveABody() && !in_class->isDefaulted() && !in_class->isDeleted()) {
        const clang::Stmt* body       = in_class->getBody();
        const unsigned     body_begin = sm.getDecomposedLoc(body->getBeginLoc()).second;
        text                          = source_text.substr(0, body_begin - decl_begin).str();
    } else {
        text = source_text.str();
    }

    const clang::AssociatedConstraint& requires_clause = in_class->getTrailingRequiresClause();
    if (strip_requires_clause && requires_clause.ConstraintExpr != nullptr) {
        const unsigned req_begin = sm.getDecomposedLoc(requires_clause.ConstraintExpr->getBeginLoc()).second;
        text                     = text.substr(0, req_begin - decl_begin);
        // The whitespace set matches std::isspace under the classic "C"
        // locale (the only locale this text is ever scanned in), stated
        // directly rather than through a per-character predicate call --
        // same spelling docblock.cpp's trim() uses.
        const auto right_trim = [&text] {
            constexpr std::string_view whitespace = " \t\n\v\f\r";
            text.resize(text.find_last_not_of(whitespace) + 1);
        };
        right_trim();
        static constexpr std::string_view kRequires = "requires";
        if (text.size() >= kRequires.size() &&
            text.compare(text.size() - kRequires.size(), kRequires.size(), kRequires) == 0) {
            text.resize(text.size() - kRequires.size());
            right_trim();
        }
    }
    text += ';';

    // Rewrites over the extracted text, in absolute offsets: the `\seebelow`
    // target sentinel and droppable namespace qualifiers.
    // Both sit before whatever the steps above trimmed from the end, so their
    // offsets into `text` remain valid; anything past the trim is discarded by
    // the bounds check below.
    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       edits;
    std::optional<SynopsisEdit>     seebelow_edit;

    if (seebelow) {
        if (const auto range = seebelow_source_range(in_class, *seebelow)) {
            const unsigned begin = sm.getDecomposedLoc(range->getBegin()).second;
            const unsigned end =
                sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range->getEnd(), 0, sm, lang_opts)).second;
            if (begin >= decl_begin && end > begin) {
                const std::string sentinel = span_sentinel(0);
                sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::SeeBelow, "SEEBELOW", ""};
                seebelow_edit              = SynopsisEdit{begin, end, sentinel};
            }
        }
    }
    edits.append_range(namespace_qualifier_edits(const_cast<clang::Decl*>(range_decl), ns_drop_set, sm, lang_opts) |
                       std::views::filter([&](const auto& p) { return p.first >= decl_begin; }) |
                       std::views::transform([](const auto& p) { return SynopsisEdit{p.first, p.second, ""}; }));
    unsigned span_n = sentinels.size();
    if (const auto marked = expos_set.find(in_class->getCanonicalDecl()); marked != expos_set.end()) {
        const clang::SourceRange name_range = in_class->getNameInfo().getSourceRange();
        const unsigned           name_begin = sm.getDecomposedLoc(name_range.getBegin()).second;
        const unsigned           name_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(name_range.getEnd(), 0, sm, lang_opts)).second;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{ir::SpanKind::ExposId, marked->second, marked->second};
        edits.push_back(SynopsisEdit{name_begin, name_end, sentinel});
    }
    // substrate generic algorithm: the same stateful sentinel/edit flat-map
    // as the synopsis path above, scoped to this declaration fragment.
    for (const ExposUse& use : expos_uses(const_cast<clang::Decl*>(range_decl), expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin)
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }
    if (seebelow_edit)
        add_dominant_edit(edits, std::move(*seebelow_edit));

    // Back to front, skipping overlaps and anything past the trimmed end.
    std::sort(
        edits.begin(), edits.end(), [](const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin > b.begin; });
    unsigned applied_begin = std::numeric_limits<unsigned>::max();
    // substrate generic algorithm: the same watermark discipline as
    // extract_synopsis's applier above, and for the same reason -- a
    // mutation whose correctness depends on descending position is a
    // scatter, not a fold in disguise.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        const std::size_t rel_begin = edit.begin - decl_begin;
        const std::size_t rel_end   = edit.end - decl_begin;
        if (rel_end > text.size())
            continue; // fell in the spliced-away body or requires-clause
        text.replace(rel_begin, rel_end - rel_begin, edit.replacement);
        applied_begin = edit.begin;
    }

    return format_and_recover(std::move(text), sentinels);
}

// A documented type alias is an ordinary itemdecl. The declaration
// itself is the AST-backed extraction range, while the TypeLoc identifies the
// RHS well enough for a dominant implementation-detail substitution. `head`
// is the enclosing TypeAliasTemplateDecl for a namespace-scope alias
// template, so the extraction starts at `template` rather than `using` —
// the same range-head override extract_synopsis takes for a class template.
beman::specgen::ir::CodeText extract_alias_itemdecl(const clang::TypeAliasDecl*                      alias,
                                                    const clang::SourceManager&                      sm,
                                                    const clang::LangOptions&                        lang_opts,
                                                    const std::set<std::string>&                     ns_drop_set,
                                                    const std::map<const clang::Decl*, std::string>& expos_set,
                                                    std::optional<AliasMask>                         mask,
                                                    const clang::Decl*                               head = nullptr) {
    namespace ir = beman::specgen::ir;

    const clang::SourceLocation begin_loc  = head != nullptr ? head->getBeginLoc() : alias->getBeginLoc();
    const unsigned              decl_begin = sm.getDecomposedLoc(begin_loc).second;
    std::string                 text       = clang::Lexer::getSourceText(
                                                 clang::CharSourceRange::getTokenRange(begin_loc, alias->getEndLoc()), sm, lang_opts)
                                                 .str();
    text += ';';

    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       edits;
    edits.append_range(
        namespace_qualifier_edits(const_cast<clang::TypeAliasDecl*>(alias), ns_drop_set, sm, lang_opts) |
        std::views::filter([&](const auto& range) { return range.first >= decl_begin; }) |
        std::views::transform([](const auto& range) { return SynopsisEdit{range.first, range.second, ""}; }));

    unsigned span_n = 0;
    // substrate generic algorithm: one AST-use rewrite may emit two ordered edits.
    for (const ExposUse& use : expos_uses(const_cast<clang::TypeAliasDecl*>(alias), expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin)
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }

    if (mask) {
        if (const auto range = alias_rhs_source_range(alias, sm, lang_opts)) {
            const unsigned begin = sm.getDecomposedLoc(range->getBegin()).second;
            const unsigned end =
                sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range->getEnd(), 0, sm, lang_opts)).second;
            if (begin >= decl_begin && end > begin) {
                const std::string  sentinel = span_sentinel(span_n++);
                const ir::SpanKind kind =
                    *mask == AliasMask::ImplDefined ? ir::SpanKind::ImplDefined : ir::SpanKind::SeeBelow;
                const std::string display = *mask == AliasMask::ImplDefined ? "implementation-defined" : "SEEBELOW";
                sentinels[sentinel]       = SpanInfo{kind, display, ""};
                add_dominant_edit(edits, SynopsisEdit{begin, end, sentinel});
            }
        }
    }

    std::sort(
        edits.begin(), edits.end(), [](const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin > b.begin; });
    unsigned applied_begin = std::numeric_limits<unsigned>::max();
    // substrate generic algorithm: descending edits share the synopsis watermark discipline.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        const std::size_t begin = edit.begin - decl_begin;
        const std::size_t end   = edit.end - decl_begin;
        if (end > text.size())
            continue;
        text.replace(begin, end - begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return format_and_recover(std::move(text), sentinels);
}

// --- Constraints derivation (design §5.1) -----------------------------------
// [structure.specifications] read backwards: Constraints = removed from
// overload resolution = the trailing requires-clause. Source is the trailing
// requires-clause only (the Beman convention); Sema-normalized associated
// constraints (constrained template params, abbreviated `auto`) are a
// refinement this tier does not attempt.

// Flatten `e`'s top-level `&&` conjuncts into `out`, in source order. Only
// `&&` is flattened — a `||` at the top level is left as a single leaf
// ("no flattening through disjunctions", design §5.1: `A && (B || C)` yields
// two conjuncts, the second verbatim).
void split_conjuncts(const clang::Expr* e, std::vector<const clang::Expr*>& out) {
    if (const auto* bo = llvm::dyn_cast<clang::BinaryOperator>(e->IgnoreParens())) {
        if (bo->getOpcode() == clang::BO_LAnd) {
            split_conjuncts(bo->getLHS(), out);
            split_conjuncts(bo->getRHS(), out);
            return;
        }
    }
    out.push_back(e);
}

// Source text of an expression's written form.
llvm::StringRef expr_text(const clang::Expr* e, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    return clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(e->getSourceRange()), sm, lang_opts);
}

beman::specgen::ir::CodeText expr_code_rewritten(const clang::Expr*                               e,
                                                 const clang::SourceManager&                      sm,
                                                 const clang::LangOptions&                        lang_opts,
                                                 const std::set<std::string>&                     ns_drop_set,
                                                 const std::map<const clang::Decl*, std::string>& expos_set) {
    const unsigned            text_begin = sm.getDecomposedLoc(e->getSourceRange().getBegin()).second;
    std::string               text       = expr_text(e, sm, lang_opts).str();
    std::vector<SynopsisEdit> edits =
        namespace_qualifier_edits(const_cast<clang::Expr*>(e), ns_drop_set, sm, lang_opts) |
        std::views::transform([](const auto& p) { return SynopsisEdit{p.first, p.second, ""}; }) |
        std::ranges::to<std::vector<SynopsisEdit>>();
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;
    // substrate generic algorithm: build a sentinel table and one-or-two edits
    // per resolved use; the shared counter couples both output collections.
    for (const ExposUse& use : expos_uses(const_cast<clang::Expr*>(e), expos_set, sm, lang_opts)) {
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }
    std::sort(
        edits.begin(), edits.end(), [](const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin > b.begin; });
    unsigned applied_begin = std::numeric_limits<unsigned>::max();
    // substrate generic algorithm: descending offset mutation with an overlap
    // watermark, identical in kind to the synopsis/itemdecl edit applicators.
    for (const SynopsisEdit& edit : edits) {
        if (edit.begin < text_begin || edit.end > text_begin + text.size() || edit.end > applied_begin)
            continue;
        text.replace(edit.begin - text_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return recover_sentinels(std::move(text), sentinels);
}

// Phrase one conjunct (design §5.1), peeled past parens/implicit casts:
//   - a single-argument concept-id reads "`X` models \libconcept{C}" (the WG21
//     spelling), X being the concept's sole template argument;
//   - a `!`-negation reads "`X` is `false`" (X being the negated subexpression,
//     without the `!`);
//   - anything else — a bool trait/variable, a zero-/multi-argument concept
//     (the single-subject "models" spelling does not apply cleanly), or an
//     unrecognized/verbatim expression such as a parenthesized disjunction —
//     reads "`X` is `true`" (or "is satisfied" for a many-arg concept).
beman::specgen::ir::Paragraph phrase_conjunct(const clang::Expr*                               leaf,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    const clang::Expr* peeled = leaf->IgnoreParenImpCasts();

    if (const auto* concept_expr = llvm::dyn_cast<clang::ConceptSpecializationExpr>(peeled)) {
        const clang::ConceptDecl*                 concept_decl = concept_expr->getNamedConcept();
        const std::string                         name         = concept_decl->getName().str();
        const clang::ASTTemplateArgumentListInfo* args         = concept_expr->getTemplateArgsAsWritten();
        ir::Paragraph                             out;
        if (args != nullptr && args->getNumTemplateArgs() == 1) {
            // "`arg` models \libconcept{name}". The sole argument is usually a
            // type (`copyable<U>`), so take its written source range directly
            // rather than assuming an expression argument.
            const llvm::StringRef subject = clang::Lexer::getSourceText(
                clang::CharSourceRange::getTokenRange(args->arguments()[0].getSourceRange()), sm, lang_opts);
            out.push_back(ir::CodeInline{ir::CodeText{subject.str(), {}}});
            out.push_back(ir::TextInline{" models "});
            if (const auto found = expos_set.find(concept_decl->getCanonicalDecl()); found != expos_set.end()) {
                out.push_back(ir::CodeInline{
                    ir::CodeText{found->second, {{0, found->second.size(), ir::SpanKind::ExposId, found->second}}}});
            } else {
                out.push_back(ir::ConceptRef{name});
            }
        } else {
            // No single subject to front; keep the whole concept-id verbatim.
            out.push_back(ir::CodeInline{expr_code_rewritten(peeled, sm, lang_opts, ns_drop_set, expos_set)});
            out.push_back(ir::TextInline{" is satisfied"});
        }
        return out;
    }

    const clang::UnaryOperator* negation = llvm::dyn_cast<clang::UnaryOperator>(peeled);
    if (negation != nullptr && negation->getOpcode() != clang::UO_LNot)
        negation = nullptr;

    const clang::Expr* code_expr = negation != nullptr ? negation->getSubExpr() : peeled;

    ir::Paragraph out;
    out.push_back(ir::CodeInline{expr_code_rewritten(code_expr, sm, lang_opts, ns_drop_set, expos_set)});
    out.push_back(ir::TextInline{" is "});
    out.push_back(ir::CodeInline{ir::CodeText{negation != nullptr ? "false" : "true", {}}});
    return out;
}

// Derive a Constraints element from `fn`'s own trailing requires-clause, or
// nullopt if it has none (or the clause somehow splits into no conjuncts at
// all). Rendering (sentence vs. itemize) is conjuncts::render_into, shared
// with Mandates (design §5.3) — not reimplemented here.
std::optional<beman::specgen::ir::DescriptionElement>
derive_constraints(const clang::FunctionDecl*                       fn,
                   const clang::SourceManager&                      sm,
                   const clang::LangOptions&                        lang_opts,
                   const std::set<std::string>&                     ns_drop_set,
                   const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    const clang::AssociatedConstraint& requires_clause = fn->getTrailingRequiresClause();
    if (requires_clause.ConstraintExpr == nullptr)
        return std::nullopt;

    std::vector<const clang::Expr*> leaves;
    split_conjuncts(requires_clause.ConstraintExpr, leaves);
    if (leaves.empty())
        return std::nullopt;

    const std::vector<ir::Paragraph> paragraphs =
        leaves | std::views::transform([&](const clang::Expr* leaf) {
            return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
        }) |
        std::ranges::to<std::vector<ir::Paragraph>>();

    ir::DescriptionElement element;
    element.kind      = ir::ElementKind::Constraints;
    element.derived   = true;
    element.conjuncts = paragraphs;
    beman::specgen::conjuncts::render_into(paragraphs, element);
    return element;
}

// --- Mandates derivation (design §5.2) --------------------------------------
// [structure.specifications] read backwards: Mandates = ill-formed (not merely
// removed from overload resolution) = the leading static_assert prefix of the
// out-of-line definition body. Consume the *maximal* run of static_asserts at
// the front of the body, flatten each condition at top-level `&&` (reusing
// split_conjuncts), and phrase every conjunct with the same rewriter
// Constraints uses — design §5.3 shares both the phrasing and the
// sentence/itemize rendering across the two derivations. Leading local aliases
// may precede the assertion run; the first statement of any other kind ends the
// prologue, so a static_assert appearing later is not a Mandate. static_assert
// *messages* are dropped, as the design specifies.
//
// This only tags the element `derived` and keeps its pre-join
// conjuncts (ir.hpp's DescriptionElement::conjuncts) -- ordering an authored
// twin after it (canonicalize, ir.cpp), merging the two into one rendered
// block (backend/latex.cpp), and the drift check that compares them
// (validate.cpp) are Tier-A code, not repeated here. Excluding the consumed
// asserts from `\effects-equiv` extraction is shared below with the body
// extraction.

const clang::Decl* single_decl(const clang::Stmt* stmt) {
    const auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt);
    return decl_stmt != nullptr && decl_stmt->isSingleDecl() ? decl_stmt->getSingleDecl() : nullptr;
}

struct MandatesPrologue {
    std::vector<const clang::DeclStmt*> assertions;
    const clang::Stmt*                  extraction_first = nullptr;
};

// One shared reading of the body prologue for both Mandates derivation and
// Equivalent-to extraction. Leading local aliases are retained source, but
// permit the immediately following maximal static_assert run to remain
// derivable. The first declaration/statement of any other kind closes the
// prologue; an assertion after that boundary is ordinary body code.
MandatesPrologue mandates_prologue(const clang::CompoundStmt* body) {
    const std::vector<const clang::Stmt*> stmts(body->body_begin(), body->body_end());
    std::size_t                           pos = 0;
    // substrate generic algorithm: the shared cursor partitions one source
    // sequence into aliases, assertions, and the unconsumed remainder.
    while (pos < stmts.size() && llvm::isa_and_nonnull<clang::TypeAliasDecl>(single_decl(stmts[pos])))
        ++pos;
    const std::size_t alias_count = pos;

    MandatesPrologue result;
    // substrate generic algorithm: continue the same partitioning cursor while
    // recording the middle run for both derivation and source deletion.
    while (pos < stmts.size() && llvm::isa_and_nonnull<clang::StaticAssertDecl>(single_decl(stmts[pos]))) {
        result.assertions.push_back(llvm::cast<clang::DeclStmt>(stmts[pos]));
        ++pos;
    }

    if (result.assertions.empty())
        result.extraction_first = stmts.empty() ? nullptr : stmts.front();
    else if (alias_count != 0)
        result.extraction_first = stmts.front();
    else if (pos < stmts.size())
        result.extraction_first = stmts[pos];
    return result;
}

std::optional<beman::specgen::ir::DescriptionElement>
derive_mandates(const clang::FunctionDecl*                       fn,
                const clang::SourceManager&                      sm,
                const clang::LangOptions&                        lang_opts,
                const std::set<std::string>&                     ns_drop_set,
                const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(fn->getBody());
    if (body == nullptr)
        return std::nullopt;

    // The conjuncts of one static_assert's condition, flattened at top-level
    // `&&` (split_conjuncts).
    const auto conjuncts_of = [](const clang::DeclStmt* stmt) {
        std::vector<const clang::Expr*> out;
        const auto*                     assert_decl = llvm::cast<clang::StaticAssertDecl>(stmt->getSingleDecl());
        split_conjuncts(assert_decl->getAssertExpr(), out);
        return out;
    };

    const MandatesPrologue                prologue = mandates_prologue(body);
    const std::vector<const clang::Expr*> leaves   = prologue.assertions | std::views::transform(conjuncts_of) |
                                                     std::views::join |
                                                     std::ranges::to<std::vector<const clang::Expr*>>();
    if (leaves.empty())
        return std::nullopt;

    const std::vector<ir::Paragraph> paragraphs =
        leaves | std::views::transform([&](const clang::Expr* leaf) {
            return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
        }) |
        std::ranges::to<std::vector<ir::Paragraph>>();

    ir::DescriptionElement element;
    element.kind      = ir::ElementKind::Mandates;
    element.derived   = true;
    element.conjuncts = paragraphs;
    beman::specgen::conjuncts::render_into(paragraphs, element);
    return element;
}

// --- class-head static_asserts (design §5.2) --------------------------------

bool is_template_parameter_pack(const clang::NamedDecl* param) {
    if (const auto* type = llvm::dyn_cast<clang::TemplateTypeParmDecl>(param))
        return type->isParameterPack();
    if (const auto* value = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(param))
        return value->isParameterPack();
    if (const auto* tmpl = llvm::dyn_cast<clang::TemplateTemplateParmDecl>(param))
        return tmpl->isParameterPack();
    return false;
}

// Spell the injected class-id from the declaration names Clang resolved. The
// condition text still comes from source tokens; this small synthesized name
// is wording context ("instantiates `optional<T>`"), not synopsis code.
std::string class_instantiation_name(const clang::CXXRecordDecl* record) {
    const clang::ClassTemplateDecl* tmpl = record->getDescribedClassTemplate();
    if (tmpl == nullptr)
        return record->getNameAsString();

    std::vector<std::string> arguments;
    // substrate generic algorithm: this is a transform with an early return
    // from the enclosing function when any parameter cannot be named; a view
    // cannot express that fallback without materializing an error channel.
    for (const clang::NamedDecl* param : *tmpl->getTemplateParameters()) {
        std::string name = param->getNameAsString();
        // An unnamed parameter has no source-level identifier with which to
        // spell an injected class-id. Keep the class name rather than invent
        // one; named parameters are the Beman-style input convention.
        if (name.empty())
            return record->getNameAsString();
        if (is_template_parameter_pack(param))
            name += "...";
        arguments.push_back(std::move(name));
    }
    if (arguments.empty())
        return record->getNameAsString();

    return std::format("{}<{}>",
                       record->getNameAsString(),
                       arguments | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>());
}

std::optional<beman::specgen::ir::FreeParagraph>
derive_class_mandates(const clang::CXXRecordDecl*                      record,
                      const clang::SourceManager&                      sm,
                      const clang::LangOptions&                        lang_opts,
                      const std::set<std::string>&                     ns_drop_set,
                      const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    std::vector<const clang::Expr*> leaves;
    // All direct assertions belong to this class's general wording, wherever
    // an access label happens to occur. Nested records are separate synopses
    // and are deliberately not recursed into.
    // substrate generic algorithm: a filtered flat-map into the shared leaves
    // accumulator; split_conjuncts recursively appends zero or more results.
    for (const clang::Decl* member : record->decls()) {
        const auto* assert_decl = llvm::dyn_cast<clang::StaticAssertDecl>(member);
        if (assert_decl != nullptr)
            split_conjuncts(assert_decl->getAssertExpr(), leaves);
    }
    if (leaves.empty())
        return std::nullopt;

    const std::vector<ir::Paragraph> conditions =
        leaves | std::views::transform([&](const clang::Expr* leaf) {
            return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
        }) |
        std::ranges::to<std::vector<ir::Paragraph>>();

    ir::Paragraph text;
    text.push_back(ir::TextInline{"A program that instantiates "});
    text.push_back(ir::CodeInline{ir::CodeText{class_instantiation_name(record), {}}});
    text.push_back(ir::TextInline{" is ill-formed unless "});
    text.append_range(beman::specgen::conjuncts::join_sentence(conditions));
    return ir::FreeParagraph{std::move(text)};
}

// --- \effects-equiv / \returns-equiv body extraction (design §4.2) ----------
// An extraction marker lowers to a description element carrying an *empty*
// EquivalentTo (lowering::append_extraction); the front end fills its code from
// the out-of-line definition body here. Per design §5.2 ("Consumed asserts are
// excluded from `\effects-equiv` extraction"), the prologue assertion run that
// derive_mandates consumes is deleted here too, while any preceding aliases are
// retained. A Mandates element and an Effects "Equivalent to:" block derived
// from one body therefore do not double-count the asserts.

// Source text of `body` from its first retained prologue statement through to
// just before the closing brace, reformatted with the draft FormatStyle.
// Extracting up to getRBracLoc() (rather than the last statement's end token)
// keeps the trailing `;` of each statement, which the statement's own source
// range excludes. Empty if nothing remains (an all-asserts or empty body).
std::vector<std::pair<unsigned, unsigned>>
conditional_body_edits(const llvm::StringRef& buffer, unsigned begin, unsigned end, const SkippedRanges& skipped) {
    std::vector<std::pair<unsigned, unsigned>> edits;
    // substrate generic algorithm: clip-and-filter into an existing edit set;
    // the optional output makes a plain transform the wrong shape.
    for (const auto& [skip_begin, skip_end] : skipped) {
        const unsigned clipped_begin = std::max(begin, skip_begin);
        const unsigned clipped_end   = std::min(end, skip_end);
        if (clipped_end > clipped_begin)
            edits.emplace_back(clipped_begin, clipped_end);
    }

    const auto is_conditional = [](std::string_view line) {
        constexpr std::string_view whitespace = " \t\r\n";
        const std::size_t          hash       = line.find_first_not_of(whitespace);
        if (hash == std::string_view::npos || line[hash] != '#')
            return false;
        const std::size_t word_begin = line.find_first_not_of(whitespace, hash + 1);
        if (word_begin == std::string_view::npos)
            return false;
        const std::size_t      word_end = line.find_first_of(whitespace, word_begin);
        const std::string_view word     = line.substr(word_begin, word_end - word_begin);
        return word == "if" || word == "ifdef" || word == "ifndef" || word == "elif" || word == "elifdef" ||
               word == "elifndef" || word == "else" || word == "endif";
    };

    // substrate generic algorithm: a position-tracking scan whose next input
    // offset is the newline discovered by the current iteration.
    for (unsigned line_begin = begin; line_begin < end;) {
        const std::size_t found = buffer.find('\n', line_begin);
        const unsigned    line_end =
            found == llvm::StringRef::npos || found >= end ? end : static_cast<unsigned>(found + 1);
        if (is_conditional(std::string_view(buffer.data() + line_begin, line_end - line_begin)))
            edits.emplace_back(line_begin, line_end);
        line_begin = line_end;
    }
    return edits;
}

// Clang's raw-comment table is the lexical boundary: comment spellings inside
// strings never enter it. Block comments have one vocabulary for the whole
// token; merged line comments are classified one physical line at a time.
// Whole-line removals absorb their newline, while trailing removals do not.
std::vector<std::pair<unsigned, unsigned>> body_comment_edits(const clang::ASTContext&    ctx,
                                                              const clang::SourceManager& sm,
                                                              const clang::LangOptions&   lang_opts,
                                                              const llvm::StringRef&      buffer,
                                                              unsigned                    begin,
                                                              unsigned                    end) {
    std::vector<std::pair<unsigned, unsigned>> edits;
    const auto*                                comments = ctx.Comments.getCommentsInFile(sm.getMainFileID());
    if (comments == nullptr)
        return edits;

    const auto deletion_range = [&](unsigned comment_begin, unsigned comment_end) {
        const std::size_t previous_newline = buffer.rfind('\n', comment_begin);
        const unsigned    line_begin =
            previous_newline == llvm::StringRef::npos ? 0 : static_cast<unsigned>(previous_newline + 1);
        unsigned deletion_begin = comment_begin;
        // substrate generic algorithm: walk one lexical boundary backward to
        // absorb indentation without inspecting any preceding source line.
        while (deletion_begin > line_begin &&
               (buffer[deletion_begin - 1] == ' ' || buffer[deletion_begin - 1] == '\t'))
            --deletion_begin;

        const bool standalone =
            buffer.substr(line_begin, deletion_begin - line_begin).find_first_not_of(" \t") == llvm::StringRef::npos;
        if (standalone) {
            deletion_begin  = line_begin;
            unsigned suffix = comment_end;
            // substrate generic algorithm: walk the same physical line forward
            // so a whole-line removal consumes its horizontal tail and newline.
            while (suffix < end && (buffer[suffix] == ' ' || buffer[suffix] == '\t'))
                ++suffix;
            if (suffix < end && buffer[suffix] == '\r')
                ++suffix;
            if (suffix < end && buffer[suffix] == '\n')
                comment_end = suffix + 1;
        }
        return std::pair{deletion_begin, comment_end};
    };

    // substrate generic algorithm: a bounded filter-map from Clang's absolute
    // comment table into zero or more deletion ranges for this fragment.
    for (const auto& entry : *comments) {
        const clang::RawComment* comment       = entry.second;
        const clang::SourceRange range         = comment->getSourceRange();
        const unsigned           comment_begin = sm.getDecomposedLoc(range.getBegin()).second;
        const unsigned           comment_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(range.getEnd(), 0, sm, lang_opts)).second;
        if (comment_end <= begin || comment_begin >= end)
            continue;

        const llvm::StringRef raw = comment->getRawText(sm);
        if (raw.ltrim(" \t").starts_with("/*")) {
            if (line_vocabulary(raw.ltrim(" \t")) != CommentVocabulary::Draft)
                edits.push_back(deletion_range(comment_begin, comment_end));
            continue;
        }

        // substrate generic algorithm: a position-tracking scan is required
        // because each merged physical line needs its absolute source offset.
        for (std::size_t line_begin = 0; line_begin < raw.size();) {
            const std::size_t     newline  = raw.find('\n', line_begin);
            const std::size_t     line_end = newline == llvm::StringRef::npos ? raw.size() : newline;
            const llvm::StringRef line     = raw.slice(line_begin, line_end).ltrim(" \t");
            if (line_vocabulary(line) != CommentVocabulary::Draft) {
                const unsigned absolute_begin = comment_begin + static_cast<unsigned>(line_begin);
                const unsigned absolute_end   = comment_begin + static_cast<unsigned>(line_end);
                edits.push_back(deletion_range(absolute_begin, absolute_end));
            }
            if (newline == llvm::StringRef::npos)
                break;
            line_begin = newline + 1;
        }
    }
    return edits;
}

// Normalize absolute source deletion ranges over one extracted fragment.
// Every producer shares this clipping/union step before semantic replacements
// are filtered against the text that will actually survive.
std::vector<std::pair<unsigned, unsigned>>
normalize_body_deletions(unsigned text_begin, unsigned text_size, std::vector<std::pair<unsigned, unsigned>> edits) {
    const unsigned text_end = text_begin + text_size;
    // substrate generic algorithm: normalize the caller-owned interval set in
    // place before the following stateful overlap fold.
    for (auto& [begin, end] : edits) {
        begin = std::max(begin, text_begin);
        end   = std::min(end, text_end);
    }
    std::erase_if(edits, [](const auto& edit) { return edit.second <= edit.first; });
    std::sort(edits.begin(), edits.end());

    std::vector<std::pair<unsigned, unsigned>> merged;
    // substrate generic algorithm: an ordered interval-union fold whose state
    // is the last range already emitted.
    for (const auto& edit : edits) {
        if (!merged.empty() && edit.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, edit.second);
        else
            merged.push_back(edit);
    }
    return merged;
}

// Apply normalized deletions and semantic replacements together, back to
// front, so every edit remains in the original main-file coordinate system.
std::string apply_body_edits(std::string                                       text,
                             unsigned                                          text_begin,
                             const std::vector<std::pair<unsigned, unsigned>>& deletions,
                             std::vector<SynopsisEdit>                         replacements) {
    replacements.append_range(deletions | std::views::transform([](const auto& deletion) {
                                  return SynopsisEdit{deletion.first, deletion.second, ""};
                              }));
    std::sort(replacements.begin(), replacements.end(), [](const SynopsisEdit& a, const SynopsisEdit& b) {
        return a.begin > b.begin || (a.begin == b.begin && a.end > b.end);
    });

    unsigned applied_begin = text_begin + static_cast<unsigned>(text.size());
    // substrate generic algorithm: descending-offset string mutation with an
    // overlap watermark; later edits determine whether earlier ones survive.
    for (const SynopsisEdit& edit : replacements) {
        if (edit.end > applied_begin)
            continue;
        text.replace(edit.begin - text_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return text;
}

beman::specgen::ir::CodeText extract_equiv_body(const clang::CompoundStmt*                       body,
                                                const clang::ASTContext&                         ctx,
                                                const clang::SourceManager&                      sm,
                                                const clang::LangOptions&                        lang_opts,
                                                const std::map<std::string, std::string>&        expos_names,
                                                const std::map<const clang::Decl*, std::string>& expos_set,
                                                const std::set<std::string>&                     ns_drop_set,
                                                const SkippedRanges&                             skipped) {
    const MandatesPrologue prologue = mandates_prologue(body);
    if (prologue.extraction_first == nullptr)
        return {};

    const unsigned begin = sm.getDecomposedLoc(prologue.extraction_first->getBeginLoc()).second;
    const unsigned end   = sm.getDecomposedLoc(body->getRBracLoc()).second;
    if (end <= begin)
        return {};

    const llvm::StringRef                      buffer = sm.getBufferData(sm.getMainFileID());
    std::vector<std::pair<unsigned, unsigned>> deletions =
        namespace_qualifier_edits(const_cast<clang::CompoundStmt*>(body), ns_drop_set, sm, lang_opts);
    deletions.append_range(conditional_body_edits(buffer, begin, end, skipped));
    deletions.append_range(body_comment_edits(ctx, sm, lang_opts, buffer, begin, end));
    deletions.append_range(prologue.assertions | std::views::transform([&](const clang::DeclStmt* statement) {
                               const unsigned statement_begin = sm.getDecomposedLoc(statement->getBeginLoc()).second;
                               const unsigned statement_end =
                                   sm.getDecomposedLoc(
                                         clang::Lexer::getLocForEndOfToken(statement->getEndLoc(), 0, sm, lang_opts))
                                       .second;
                               return std::pair{statement_begin, statement_end};
                           }));

    const std::vector<ExposUse> semantic_uses =
        expos_uses(const_cast<clang::CompoundStmt*>(body), expos_set, sm, lang_opts);
    deletions.append_range(
        semantic_uses |
        std::views::filter([](const ExposUse& use) { return use.qualifier_end > use.qualifier_begin; }) |
        std::views::transform([](const ExposUse& use) { return std::pair{use.qualifier_begin, use.qualifier_end}; }));
    deletions = normalize_body_deletions(begin, end - begin, std::move(deletions));

    unsigned                        span_n = 0;
    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       replacements;
    const auto                      is_deleted = [&](unsigned use_begin, unsigned use_end) {
        return std::ranges::any_of(
            deletions, [&](const auto& deletion) { return use_begin < deletion.second && deletion.first < use_end; });
    };
    // substrate generic algorithm: each surviving semantic use allocates a
    // unique sentinel and appends its coordinated replacement metadata.
    for (const ExposUse& use : semantic_uses) {
        if (use.name_begin < begin || use.name_end > end || is_deleted(use.name_begin, use.name_end))
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{beman::specgen::ir::SpanKind::ExposId, use.display, use.display};
        replacements.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
    }
    const std::string text =
        apply_body_edits(buffer.substr(begin, end - begin).str(), begin, deletions, std::move(replacements));

    // Dependent member uses can lack a resolved DeclRefExpr; keep design
    // §3.5's class-local textual fallback after the semantic namespace pass.
    std::string sentineled = apply_expos_sentinels(text, expos_names, span_n, sentinels);

    // The range runs up to the closing brace, so it carries the trailing
    // newline/indentation before `}`; drop it. CodeText holds the block body
    // without a trailing newline (as the synopsis does) — render_codeblock
    // supplies the newline before `\end{codeblock}`, so a trailing one here
    // would print a blank line.
    beman::specgen::ir::CodeText out = format_and_recover(std::move(sentineled), sentinels);
    out.text.erase(out.text.find_last_not_of(" \t\n") + 1);
    return out;
}

// Fill every description element that carries an empty EquivalentTo (an
// `\effects-equiv` / `\returns-equiv` slot left by lowering) from `fn`'s
// definition body. Both markers extract the same body-after-prefix text —
// `\returns-equiv` is authored for a single-return body, so that text is a lone
// `return E;`. Elements whose EquivalentTo already has text (e.g. from
// round-tripped IR) are untouched.
void fill_equiv_bodies(const clang::FunctionDecl*                       fn,
                       beman::specgen::ir::ItemDescr&                   descr,
                       const clang::SourceManager&                      sm,
                       const clang::LangOptions&                        lang_opts,
                       const std::map<std::string, std::string>&        expos_names,
                       const std::map<const clang::Decl*, std::string>& expos_set,
                       const std::set<std::string>&                     ns_drop_set,
                       const SkippedRanges&                             skipped) {
    namespace ir = beman::specgen::ir;

    const auto is_empty_slot = [](const ir::DescriptionElement& el) {
        return el.equivalent.has_value() && el.equivalent->code.text.empty();
    };
    if (std::none_of(descr.elements.begin(), descr.elements.end(), is_empty_slot))
        return;

    const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(fn->getBody());
    if (body == nullptr)
        return;

    const ir::CodeText code =
        extract_equiv_body(body, fn->getASTContext(), sm, lang_opts, expos_names, expos_set, ns_drop_set, skipped);
    // substrate generic algorithm: a for_each-shaped mutation of the elements
    // it iterates, not a container rebuild -- filtering states the predicate
    // up front instead of burying it in the loop body's if, but this tree
    // declines ranges::for_each for exactly the reason frontend.cpp's
    // orphaned-label walk does: wrapping the assignment in a
    // call would hide the loop, not remove it.
    //
    // Note the body **falsifies is_empty_slot for the element it just
    // visited**. That is safe, but by specification rather than by
    // construction: filter_view::iterator::operator++ advances past the
    // current element *before* searching for the next match, so the now-
    // non-matching element is never re-tested, and no element is skipped or
    // revisited. Anything that reorders this -- re-reading the filtered range,
    // or caching an iterator across the assignment -- loses that guarantee.
    for (ir::DescriptionElement& el : descr.elements | std::views::filter(is_empty_slot))
        el.equivalent->code = code;
}

// Result of build_spec_item: the SpecItem it built (possibly empty), the
// directives its docblock carried (default-constructed if it had none), and
// whether it attached an out-of-line function definition at all — the caller
// (classify(), the pipeline's first stage) needs that last bit to decide
// whether `\also`/empty-descr grouping and `\omit` apply; a non-function
// decl's empty SpecItem must never be treated as a group primary or a
// candidate for omission.
struct AttachedItem {
    beman::specgen::ir::SpecItem             item;
    beman::specgen::lowering::ItemDirectives directives;
    bool                                     is_function_def = false;
    // Whatever this item's docblock grammar reported, positioned in
    // the main file. Carried out with the item rather than printed here: the
    // front end builds IR and says nothing to a terminal, so reporting is the
    // driver's, reached through document_build::BuildResult::diagnostics.
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
    // Placement key: the byte offset of the item's *in-class*
    // declaration within the main file — getFirstDecl() for an out-of-line
    // definition, the friend/member itself for an in-class one. Both are class-
    // body offsets, so ordering items by this key is class-body order (design
    // §3.3), the axis that interleaves in-class members with their out-of-line
    // siblings.
    unsigned inclass_offset = 0;
    unsigned grouping_line  = 0; // first line of the docblock carrying grouping metadata
};

// Parse and lower `decl`'s own `//!` docblock into `attached` — descr,
// directives, diagnostics, and the grouping line — if it carries one. The
// shared front half of the attach_* functions whose markup and itemdecl come
// off one decl (attach_alias, attach_record_declaration,
// attach_namespace_entity). attach_function keeps its own copy inline: its
// markup decl and itemdecl decl differ, and it reads the directives
// mid-flight to decide requires-clause stripping.
void attach_docblock(AttachedItem& attached, const clang::Decl* decl, const clang::SourceManager& sm) {
    namespace grammar  = beman::specgen::grammar;
    namespace lowering = beman::specgen::lowering;

    const clang::RawComment* rc = decl->getASTContext().getRawCommentForDeclNoCache(decl);
    if (rc == nullptr)
        return;
    const llvm::StringRef            raw      = rc->getRawText(sm);
    const std::optional<std::size_t> start    = docblock_start(raw);
    const llvm::StringRef            raw_text = start ? raw.substr(*start) : llvm::StringRef{};
    if (raw_text.empty())
        return;
    attached.grouping_line = sm.getSpellingLineNumber(rc->getBeginLoc().getLocWithOffset(static_cast<int>(*start)));
    const grammar::ParseResult pr      = grammar::parse_docblock(raw_text.str());
    lowering::Lowered          lowered = lowering::lower(pr.block);
    attached.diagnostics               = docblock_diagnostics(rc, *start, pr.diags, sm);
    attached.item.descr                = std::move(lowered.descr);
    attached.directives                = std::move(lowered.directives);
}

// Build the AttachedItem for one function, given the decl its markup/body/
// derivations come from (`def`) and the decl its itemdecl *text* comes from
// (`decl_form`). For an out-of-line definition those differ — markup at the
// definition (clause order), itemdecl from the in-class declaration (design
// §3.3) — so `def` is the definition and `decl_form` is `def->getFirstDecl()`.
// For an in-class definition (a hidden friend or a compiler-bug member)
// the definition *is* its own first declaration, so both are the same decl and
// `friend_begin` is the enclosing FriendDecl's begin location (invalid for a
// plain member) so the itemdecl keeps `friend`.
AttachedItem attach_function(const clang::FunctionDecl*                       def,
                             const clang::FunctionDecl*                       decl_form,
                             clang::SourceLocation                            friend_begin,
                             const clang::SourceManager&                      sm,
                             const clang::LangOptions&                        lang_opts,
                             const std::set<std::string>&                     ns_drop_set,
                             const std::map<const clang::Decl*, std::string>& expos_set,
                             const SkippedRanges&                             skipped) {
    namespace ir       = beman::specgen::ir;
    namespace grammar  = beman::specgen::grammar;
    namespace lowering = beman::specgen::lowering;

    AttachedItem attached;
    attached.is_function_def = true;
    attached.inclass_offset =
        sm.getDecomposedLoc(friend_begin.isValid() ? friend_begin : decl_form->getBeginLoc()).second;

    // Descr: the definition's own `//!` docblock, if it has one.
    // getRawCommentForDeclNoCache attaches the immediately preceding comment;
    // is_docblock_comment guards against picking up a draft-form `\ref`/
    // `\rSec` comment that happens to sit just above the definition. Read
    // ahead of the itemdecl (below) because `\constraints-in-decl`, carried
    // on `attached.directives`, decides whether the requires-clause is
    // stripped from it.
    if (const clang::RawComment* rc = def->getASTContext().getRawCommentForDeclNoCache(def)) {
        const llvm::StringRef            raw      = rc->getRawText(sm);
        const std::optional<std::size_t> start    = docblock_start(raw);
        const llvm::StringRef            raw_text = start ? raw.substr(*start) : llvm::StringRef{};
        if (!raw_text.empty()) {
            attached.grouping_line =
                sm.getSpellingLineNumber(rc->getBeginLoc().getLocWithOffset(static_cast<int>(*start)));
            const grammar::ParseResult pr      = grammar::parse_docblock(raw_text.str());
            lowering::Lowered          lowered = lowering::lower(pr.block);
            // The findings parse_docblock produced — the element-ordering
            // Note, the duplicate-element Warning, the unknown-tag Error —
            // carried out so the driver can print them.
            attached.diagnostics = docblock_diagnostics(rc, *start, pr.diags, sm);
            // \omit and \also (design §4.3) are acted on by classify() and
            // document_build::group_items (pipeline stages 1/3), which have
            // the tree context this function does not. \describe/\at/\merge
            // act on in-class-defined members and remain unread here.
            attached.item.descr = std::move(lowered.descr);
            attached.directives = std::move(lowered.directives);
            if (attached.directives.impdef) {
                const std::vector<grammar::Diagnostic> invalid{
                    {beman::specgen::Severity::Error, 0, "\\impdef applies only to type aliases"}};
                attached.diagnostics.append_range(docblock_diagnostics(rc, *start, invalid, sm));
            }
        }
    }

    // Itemdecl: the in-class declaration's own text (design §3.3), one
    // signature per SpecItem here — `\also` grouping appends further
    // signatures onto a group's primary in document_build::group_items, not
    // here. Default
    // (no `\constraints-in-decl`): the requires-clause is stripped, since it
    // is derived into a Constraints element below instead (design §5.1).
    const bool strip_requires = !attached.directives.constraints_in_decl;
    attached.item.decl.signatures.push_back(extract_itemdecl(decl_form,
                                                             sm,
                                                             lang_opts,
                                                             strip_requires,
                                                             ns_drop_set,
                                                             expos_set,
                                                             friend_begin,
                                                             seebelow_target(attached.directives)));

    // The declaration kind determines the draft index form without
    // inspecting its formatted spelling. Use decl_form -- the declaration the
    // itemdecl renders -- rather than the out-of-line definition.
    if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(decl_form)) {
        attached.item.decl.index.push_back({ir::IndexKind::Constructor, ctor->getParent()->getNameAsString(), {}});
    } else if (const auto* dtor = llvm::dyn_cast<clang::CXXDestructorDecl>(decl_form)) {
        attached.item.decl.index.push_back({ir::IndexKind::Destructor, dtor->getParent()->getNameAsString(), {}});
    } else if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(decl_form)) {
        attached.item.decl.index.push_back(
            {ir::IndexKind::Member, method->getNameAsString(), method->getParent()->getNameAsString()});
    } else if (const std::string name = decl_form->getNameAsString(); !name.empty()) {
        attached.item.decl.index.push_back({ir::IndexKind::Global, name, {}});
    }

    // An authored Constraints/Mandates replaces its derived twin. Preserve the
    // derivation's conjuncts on that authored element as validator-only drift
    // evidence; conjuncts are not rendered or leakage-checked.
    const auto attach_derivation = [&](std::optional<ir::DescriptionElement> derived) {
        if (!derived)
            return;
        const auto authored = std::ranges::find_if(attached.item.descr.elements, [&](const auto& element) {
            return element.kind == derived->kind && !element.derived;
        });
        if (authored != attached.item.descr.elements.end())
            authored->conjuncts = std::move(derived->conjuncts);
        else
            attached.item.descr.elements.push_back(std::move(*derived));
        ir::canonicalize(attached.item.descr);
    };

    // Constraints (design §5.1): derive from the definition's own trailing
    // requires-clause unless `\constraints-in-decl` leaves it in the itemdecl.
    if (strip_requires) {
        attach_derivation(derive_constraints(def, sm, lang_opts, ns_drop_set, expos_set));
    }

    // Mandates (design §5.2): derive from the definition body's static_assert
    // prefix, independent of the requires-clause handling above, and re-fold
    // into canonical [structure.specifications] order.
    attach_derivation(derive_mandates(def, sm, lang_opts, ns_drop_set, expos_set));

    // \effects-equiv / \returns-equiv (design §4.2): fill the empty
    // EquivalentTo lowering left on the marker's element from the definition
    // body, excluding the static_assert prefix the Mandates derivation above
    // consumed (design §5.2). A no-op when the docblock carried no extraction
    // marker.
    // Expos uses (design §3.5/§4.1): within this class's own fragments,
    // uses of its exposition-only members render as \exposid — in the extracted
    // equiv body, and in backticked prose.
    const std::map<std::string, std::string> expos_names = expos_spellings(enclosing_record(def), expos_set);
    fill_equiv_bodies(def, attached.item.descr, sm, lang_opts, expos_names, expos_set, ns_drop_set, skipped);
    rewrite_prose_expos(attached.item.descr, expos_names);

    return attached;
}

// `head` is the enclosing TypeAliasTemplateDecl for a namespace-scope alias
// template — the decl the docblock hangs off and the extraction range starts
// at; null for a plain alias, in class or out.
AttachedItem attach_alias(const clang::TypeAliasDecl*                      alias,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::set<std::string>&                     ns_drop_set,
                          const std::map<const clang::Decl*, std::string>& expos_set,
                          const clang::TypeAliasTemplateDecl*              head = nullptr) {
    const clang::Decl* anchor = head != nullptr ? static_cast<const clang::Decl*>(head) : alias;

    AttachedItem attached;
    attached.inclass_offset = sm.getDecomposedLoc(anchor->getBeginLoc()).second;

    attach_docblock(attached, anchor, sm);
    if (attached.directives.seebelow_target && attached.grouping_line > 0)
        // Positioned at the docblock's first line, the same place
        // docblock_diagnostics puts a line-0 grammar finding.
        attached.diagnostics.push_back(
            {beman::specgen::Severity::Error, attached.grouping_line, "a type alias accepts only bare \\seebelow"});

    attached.item.decl.signatures.push_back(
        extract_alias_itemdecl(alias, sm, lang_opts, ns_drop_set, expos_set, alias_mask(attached.directives), head));
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(alias->getDeclContext())) {
        attached.item.decl.index.push_back(
            {ir::IndexKind::Member, alias->getNameAsString(), record->getNameAsString()});
    } else if (alias->getDeclContext()->isFileContext()) {
        attached.item.decl.index.push_back({ir::IndexKind::Global, alias->getNameAsString(), {}});
    }
    rewrite_prose_expos(attached.item.descr,
                        expos_spellings(llvm::dyn_cast<clang::CXXRecordDecl>(alias->getDeclContext()), expos_set));
    return attached;
}

// Build the AttachedItem for a documented record declaration the header never
// defines (design §6): an undefined class-template primary — the normal way
// to write an algebra whose operations a model must register, deliberately
// left undefined so an unregistered type fails a concept — or its non-template
// counterpart. Its wording is an ordinary itemdecl: the declaration through
// its semicolon, plus whatever description the docblock carries. `decl` is
// the top-level decl (the ClassTemplateDecl for a template, so the extraction
// starts at `template` and the docblock is looked up where classify's
// definition arms look it up); `record` supplies the class/struct/union tag
// that keeps the template head on its own line. No index metadata: a
// *defined* record's Synopsis carries none either, and the two should index
// alike or not at all.
AttachedItem attach_record_declaration(const clang::NamedDecl*                          decl,
                                       const clang::CXXRecordDecl*                      record,
                                       const clang::SourceManager&                      sm,
                                       const clang::LangOptions&                        lang_opts,
                                       const std::set<std::string>&                     ns_drop_set,
                                       const std::map<const clang::Decl*, std::string>& expos_set) {
    AttachedItem attached;
    attached.inclass_offset = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    attach_docblock(attached, decl, sm);

    const llvm::StringRef tag = record->getKindName();
    attached.item.decl.signatures.push_back(
        extract_freestanding_declaration(decl,
                                         sm,
                                         lang_opts,
                                         ns_drop_set,
                                         expos_set,
                                         /*exposition=*/false,
                                         std::string_view(tag.data(), tag.size())));
    return attached;
}

// Build the AttachedItem for a documented namespace-scope concept, variable,
// or variable template (design §6): like the record declaration above, the
// declaration *is* the wording — extracted whole through its semicolon,
// constraint and initializer kept, the way the draft writes them
// ([concept.same], [tuple.helper]). Aliases go through attach_alias instead,
// which owns the alias masking rules; either way the item indexes as a
// library global, the way a documented free function does.
AttachedItem attach_namespace_entity(const clang::NamedDecl*                          decl,
                                     const clang::SourceManager&                      sm,
                                     const clang::LangOptions&                        lang_opts,
                                     const std::set<std::string>&                     ns_drop_set,
                                     const std::map<const clang::Decl*, std::string>& expos_set) {
    AttachedItem attached;
    attached.inclass_offset = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    attach_docblock(attached, decl, sm);
    attached.item.decl.signatures.push_back(
        extract_freestanding_declaration(decl, sm, lang_opts, ns_drop_set, expos_set, /*exposition=*/false));
    attached.item.decl.index.push_back({ir::IndexKind::Global, decl->getNameAsString(), {}});
    return attached;
}

// --- Shared unwrapping projections ------------------------------------------
//
// collect_inclass_items below and the build_omit_set/build_expos_set/
// build_seebelow_map pre-passes further down each walk a class's members
// looking for a function or a hidden friend, or walk `decls` looking for an
// out-of-line function definition or a class/class-template definition. Their
// own comments already say so ("Same pre-pass shape as build_omit_set"). The
// projections below are that repeated dyn_cast unwrapping, named once each;
// the caller-specific predicate (does the docblock say \omit, \expos,
// \seebelow; does the member carry a real body) stays local to each caller,
// since that is the part that is not actually shared.

// The out-of-line function a top-level decl defines: itself if a
// FunctionDecl, or a FunctionTemplateDecl's templated declaration.
// build_expos_set has no out-of-line case of its own (exposition-only
// applies only to members), so only build_omit_set and build_seebelow_map
// use this.
const clang::FunctionDecl* as_out_of_line_function(const clang::Decl* decl) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return fn;
    if (const auto* ft = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return ft->getTemplatedDecl();
    return nullptr;
}

// A class or class template's templated record, if `decl` names either.
// Shared by all three set-builder pre-passes' outer walk.
const clang::CXXRecordDecl* as_record_decl(const clang::Decl* decl) {
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
        return record;
    if (const auto* ct = llvm::dyn_cast<clang::ClassTemplateDecl>(decl))
        return ct->getTemplatedDecl();
    return nullptr;
}

// The function a member decl names without unwrapping a function template.
// build_seebelow_map uses this as its first projection before handling a plain
// FunctionTemplateDecl explicitly.
const clang::FunctionDecl* member_as_function_or_friend(const clang::Decl* member) {
    if (const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member))
        return llvm::dyn_cast_or_null<clang::FunctionDecl>(friend_decl->getFriendDecl());
    return llvm::dyn_cast<clang::FunctionDecl>(member);
}

// Build the SpecItem for a top-level decl that is not a class/class template
// (design §3.3, §6 "Public member, out-of-line def"). `decl` may be a plain
// FunctionDecl or a FunctionTemplateDecl (a member function template of a
// class, or of a class template, defined out-of-line arrives this way; a
// non-template member of a class template, like `box<T>::get()`, does not —
// it is a plain FunctionDecl whose lexical text happens to carry the class's
// template head).
//
// An out-of-line member definition is attached as before. A namespace-scope
// free-function definition is attached only when it carries a docblock: public
// wording is explicit, while unannotated implementation helpers remain absent.
// Both the semantic and lexical contexts must be file contexts. The lexical
// half excludes hidden friends, which are semantic namespace functions but
// belong to collect_inclass_items because their source declaration is in a
// class body. Ordinary in-class definitions are handled there too.
AttachedItem build_spec_item(clang::Decl*                                     decl,
                             const clang::SourceManager&                      sm,
                             const clang::LangOptions&                        lang_opts,
                             const std::set<std::string>&                     ns_drop_set,
                             const std::map<const clang::Decl*, std::string>& expos_set,
                             const SkippedRanges&                             skipped) {
    const clang::FunctionDecl* fn = llvm::dyn_cast<clang::FunctionDecl>(decl);
    if (fn == nullptr) {
        if (const auto* fn_tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
            fn = fn_tmpl->getTemplatedDecl();
    }

    if (fn == nullptr || !fn->isThisDeclarationADefinition())
        return {};

    const auto* method             = llvm::dyn_cast<clang::CXXMethodDecl>(fn);
    const bool  out_of_line_member = method != nullptr && method->isOutOfLine();
    const bool  documented_free    = method == nullptr && fn->getDeclContext()->isFileContext() &&
                                     fn->getLexicalDeclContext()->isFileContext() && has_docblock(fn, sm);
    if (!out_of_line_member && !documented_free)
        return {};

    // Markup/body/derivations come from the definition; itemdecl text comes
    // from its first declaration (design §3.3). A free function stays ordered
    // where its documented definition appears, even when its first declaration
    // is earlier. Not a hidden friend, so there is no friend-begin override.
    AttachedItem attached =
        attach_function(fn, fn->getFirstDecl(), /*friend_begin=*/{}, sm, lang_opts, ns_drop_set, expos_set, skipped);
    if (documented_free)
        attached.inclass_offset = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    return attached;
}

// --- in-class-member collection (design §3.3 sub-case, §6) ------------------

// Collect the in-class function definitions and hidden friends that carry a
// `//!` docblock (design §3.3 sub-case, §6), including member function
// templates whose definition must remain in the class body.
// Each is attached from its in-class definition (markup, body, derivations, and
// itemdecl all come from the one decl) and appended to `pending` as a
// document_build::PendingItem, tagged with the stable name of the nearest
// preceding `\ref{...}` group in the class body — the section its wording
// belongs in. document_build::build_tree (stage 2) injects it when that
// section closes, ordered against out-of-line siblings by class-body offset.
//
// Skipped: members with no docblock; the bodyless in-class *declaration* of an
// out-of-line member (attached from its out-of-line definition instead); and
// `= default`/`= delete` members *unless* their docblock carries `\describe`
// (design §4.3, §6 — defaulted/deleted entities get an itemdecl only when asked
// for). An `\omit`/`\merge` member is dropped here (no itemdescr; its synopsis
// line is removed by the omit-set pre-pass). Placement is the nearest preceding
// `\ref{...}` group, or `\at <stable>` when the docblock overrides it; a
// member routed to a stable name that names no `\rSec` section is stashed under
// a key no frame will close on and is silently dropped (a coverage
// concern for the validator).
//
// `diagnostics` receives every collected member's docblock findings,
// including those of members this pass then skips — an `\omit`ted member's
// markup can still be malformed. They ride the enclosing SynopsisDecl rather
// than the PendingItem for the same reason: a member dropped for want of a
// section must not take its diagnostics down with it.
void collect_inclass_items(const clang::CXXRecordDecl*                               record,
                           const clang::SourceManager&                               sm,
                           const clang::LangOptions&                                 lang_opts,
                           const std::set<std::string>&                              ns_drop_set,
                           const std::map<const clang::Decl*, std::string>&          expos_set,
                           const SkippedRanges&                                      skipped,
                           std::vector<beman::specgen::document_build::PendingItem>& pending,
                           std::vector<beman::specgen::document_build::Diagnostic>&  diagnostics) {
    // The `\ref` group headers inside the class body, by offset — the same
    // class span and comment source extract_synopsis walks.
    const unsigned              class_begin = sm.getDecomposedLoc(record->getBeginLoc()).second;
    const clang::SourceLocation end_of_brace =
        clang::Lexer::getLocForEndOfToken(record->getEndLoc(), 0, sm, lang_opts);
    const unsigned class_end = sm.getDecomposedLoc(end_of_brace).second;

    // ascending by offset -- getCommentsInFile is offset-keyed, and neither
    // the filter nor the transform below reorders.
    std::vector<std::pair<unsigned, std::string>> ref_groups;
    if (const std::map<unsigned, clang::RawComment*>* comments =
            record->getASTContext().Comments.getCommentsInFile(sm.getMainFileID())) {
        ref_groups = *comments | std::views::filter([&](const auto& kv) {
            return kv.first >= class_begin && kv.first < class_end && parse_ref(kv.second->getRawText(sm)).has_value();
        }) | std::views::transform([&](const auto& kv) {
            return std::pair<unsigned, std::string>{kv.first, *parse_ref(kv.second->getRawText(sm))};
        }) | std::ranges::to<std::vector<std::pair<unsigned, std::string>>>();
    }

    // Stable name of the nearest `\ref` group at or before `offset`, else "".
    // ref_groups is ascending, so the answer is the element just before the
    // partition point of "ref_offset <= offset" -- a binary search, not the
    // linear scan-with-break this replaces.
    const auto section_for = [&ref_groups](unsigned offset) -> std::string {
        const auto it = std::ranges::partition_point(ref_groups, [&](const auto& rg) { return rg.first <= offset; });
        return it == ref_groups.begin() ? std::string{} : std::prev(it)->second;
    };

    // substrate generic algorithm: whether a member is collected, and what it
    // is collected as, is a five-step conditional computation -- unwrap it,
    // require a real body or defaulted/deleted, require a docblock, call
    // attach_function, then two more directive-driven skip conditions decide
    // whether the resulting AttachedItem is actually pushed -- over
    // class-body declaration order that the pending list's placement is
    // contractually keyed on. This is a filter-map with no
    // std::ranges name for it; the optional pattern that would fake one reads
    // worse than this loop, the same call extract_synopsis's docblock-comment
    // walk makes.
    bool previous_was_routed_alias = false;
    // substrate generic algorithm: the alias adjacency state is part of this filter-map.
    for (const clang::Decl* member : record->decls()) {
        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(member)) {
            const bool adjacent_to_alias = previous_was_routed_alias;
            previous_was_routed_alias    = false;
            if (!has_docblock(alias, sm))
                continue;

            AttachedItem attached = attach_alias(alias, sm, lang_opts, ns_drop_set, expos_set);
            diagnostics.append_range(std::move(attached.diagnostics));
            if (attached.directives.omit || attached.directives.merge)
                continue;

            const std::string section = attached.directives.at_anchor.value_or(section_for(attached.inclass_offset));
            pending.push_back(
                beman::specgen::document_build::PendingItem{section,
                                                            attached.inclass_offset,
                                                            std::move(attached.item),
                                                            true,
                                                            attached.directives.also && adjacent_to_alias});
            previous_was_routed_alias = true;
            continue;
        }
        previous_was_routed_alias = false;

        clang::SourceLocation      friend_begin; // keeps `friend` in the itemdecl
        const clang::FunctionDecl* fn = member_function_or_template(member);
        if (fn != nullptr) {
            if (const auto* friend_decl = llvm::dyn_cast<clang::FriendDecl>(member))
                friend_begin = friend_decl->getBeginLoc();
        }
        if (fn == nullptr)
            continue;
        // An in-class member is collected if it has a real (compound) body — a
        // hidden friend or a compiler-bug member — or is `= default`/`= delete`
        // (a candidate for `\describe`, checked after lowering). A bodyless
        // plain declaration belongs to an out-of-line definition, attached from
        // the top-level stream instead. (isOutOfLine() is deliberately *not*
        // checked — a hidden friend is lexically in the class but semantically
        // in the enclosing namespace, so it reports out-of-line yet is exactly
        // what this pass must collect.)
        const bool is_defaulted_or_deleted = fn->isDefaulted() || fn->isDeleted();
        const bool has_compound_body       = fn->doesThisDeclarationHaveABody() && !is_defaulted_or_deleted;
        if (!has_compound_body && !is_defaulted_or_deleted)
            continue;
        if (!has_docblock(fn, sm))
            continue;

        AttachedItem attached = attach_function(fn, fn, friend_begin, sm, lang_opts, ns_drop_set, expos_set, skipped);
        // The member's docblock findings, taken before any of the
        // skip conditions below: whether this member ends up with wording is
        // a separate question from whether its markup parsed cleanly, and
        // three of the four ways out of this loop are `continue`.
        diagnostics.append_range(std::move(attached.diagnostics));
        // A `= default`/`= delete` member earns an itemdecl only when there is
        // something to specify about it — an authored description element
        // (design §6, cf. tuple's defaulted copy constructor, whose itemdecl
        // exists because a Mandates and an Effects apply to it) — or an explicit
        // `\describe`. Otherwise it stays a synopsis-only declaration. The
        // `= default`/`= delete` tail is kept verbatim in the itemdecl.
        if (is_defaulted_or_deleted && attached.item.descr.elements.empty() && !attached.directives.describe)
            continue;
        // `\omit`/`\merge`: no itemdescr (the synopsis line is dropped by the
        // omit-set pre-pass).
        if (attached.directives.omit || attached.directives.merge)
            continue;
        // Placement: `\at <stable>` overrides the inferred `\ref` group.
        const std::string section = attached.directives.at_anchor.value_or(section_for(attached.inclass_offset));
        pending.push_back(
            beman::specgen::document_build::PendingItem{section, attached.inclass_offset, std::move(attached.item)});
    }
}

void group_adjacent_aliases(std::vector<beman::specgen::document_build::PendingItem>& pending) {
    namespace db = beman::specgen::document_build;
    std::vector<db::PendingItem> grouped;
    grouped.reserve(pending.size());
    // substrate generic algorithm: a follower mutates its preceding routed primary.
    for (db::PendingItem& candidate : pending) {
        if (candidate.is_alias && candidate.wants_join && !grouped.empty() && grouped.back().is_alias &&
            grouped.back().stable == candidate.stable) {
            db::append_grouped_itemdecl(grouped.back().item.decl, std::move(candidate.item.decl));
            continue;
        }
        grouped.push_back(std::move(candidate));
    }
    pending = std::move(grouped);
}

// --- the coverage roster (design §9) ----------------------------------------

// The decl a member's markup is written on: the out-of-line definition when
// there is one, else the member itself. This is why the roster needs no
// pre-pass of its own — `getDefinition()` reaches the definition from the
// in-class declaration directly, and by the time any of this runs the whole
// header has been parsed, so the redeclaration chain is complete.
const clang::FunctionDecl* markup_decl(const clang::FunctionDecl* fn) {
    const clang::FunctionDecl* def = fn->getDefinition();
    return def != nullptr ? def : fn;
}

// Enumerate `record`'s class-body declarations and what became of each one
// (design §9). A synopsis is formatted code text by
// the time it leaves this tier, so nothing downstream can enumerate the
// declarations inside it; this roster carries that enumeration alongside the
// text, which is what lets the coverage invariant be *checked* in Tier A, with
// no Clang in the build (validate/validate.cpp).
//
// Observations only — the rule is not applied here. In particular
// `Undocumented` means "the front end found nothing accounting for this
// member", not "this is an error"; deciding that is the validator's job.
//
// `pending` is what collect_inclass_items just produced for this same class,
// which is where two facts come from that are otherwise unrecoverable: whether
// an in-class member's markup actually yielded an itemdescr (a `= default`
// member with a contentless docblock does not), and the stable name its
// wording was *routed* to. That routing is the only record that it happened at
// all — document_build::build_tree drops a member routed to a stable name no
// `\rSec` opens, leaving nothing else behind to see.
//
// Unmarked nested types, aliases and class-head `static_assert`s get no entry.
// A marked type alias is different: it has routed wording, and the roster
// records that observation as MemberKind::Alias.
std::vector<beman::specgen::ir::SynopsisEntry>
build_roster(const clang::CXXRecordDecl*                                     record,
             const clang::SourceManager&                                     sm,
             const std::map<const clang::Decl*, std::string>&                expos_set,
             const std::vector<beman::specgen::document_build::PendingItem>& pending) {
    namespace ir = beman::specgen::ir;

    // The class-body offsets collect_inclass_items keyed its pending items on
    // (AttachedItem::inclass_offset), mapped to the section each was routed to.
    const std::map<unsigned, std::string> routed =
        pending | std::views::transform([](const beman::specgen::document_build::PendingItem& item) {
            return std::pair<unsigned, std::string>{item.offset, item.stable};
        }) |
        std::ranges::to<std::map<unsigned, std::string>>();

    std::vector<ir::SynopsisEntry> roster;

    // substrate generic algorithm: a fold into `roster` over `record`'s own
    // class-body order -- the same walk, in the same contractual order,
    // extract_synopsis and collect_inclass_items make, with a
    // first-match-wins ladder per member. A views pipeline would have to
    // reproduce the whole ladder inside its projection.
    for (const RealRecordMember& real_member : real_record_members(record)) {
        const clang::Decl* member = real_member.decl;
        if (llvm::isa<clang::AccessSpecDecl>(member))
            continue;

        // Anonymous records are transparent only for declarations that the
        // synopsis exposes. Their unmarked alternatives are not direct class
        // declarations and therefore are outside the coverage roster.
        if (real_member.nested_anonymous) {
            const auto* named = llvm::dyn_cast<clang::NamedDecl>(member);
            if (named == nullptr || !expos_set.contains(named->getCanonicalDecl()))
                continue;
        }

        // What this member declares, if it is something the draft would write
        // wording for: a function (however wrapped), a data member, or a
        // static data member. Anything else has no entry.
        const clang::FunctionDecl* fn    = member_function_or_template(member);
        const clang::NamedDecl*    named = fn;
        const auto*                alias = llvm::dyn_cast<clang::TypeAliasDecl>(member);
        if (alias != nullptr && !has_docblock(alias, sm))
            continue;
        if (named == nullptr)
            named = alias;
        if (named == nullptr)
            named = llvm::dyn_cast<clang::FieldDecl>(member);
        if (named == nullptr)
            named = llvm::dyn_cast<clang::VarDecl>(member); // static data member
        if (named == nullptr)
            continue; // unmarked nested type or static_assert: no wording of its own

        // The placement key collect_inclass_items keyed its pending items on
        // (AttachedItem::inclass_offset): the enclosing FriendDecl for a hidden
        // friend, whose `friend` keyword is not part of the inner FunctionDecl's
        // range, else the decl's own begin.
        const clang::Decl* key_decl  = llvm::isa<clang::FriendDecl>(member) ? member : named;
        const auto         routed_to = routed.find(sm.getDecomposedLoc(key_decl->getBeginLoc()).second);

        // Markup is read from the definition when there is one (a marker is
        // written where the wording is), else from the declaration itself.
        const beman::specgen::lowering::ItemDirectives directives =
            docblock_directives(fn != nullptr ? markup_decl(fn) : named, sm);
        const clang::FunctionDecl* definition = fn != nullptr ? fn->getDefinition() : nullptr;

        ir::SynopsisEntry entry;
        entry.name = named->getNameAsString();
        // `fn` is the FunctionDecl this member resolved to, so its absence is
        // exactly "this is a data member" -- a FieldDecl or a static VarDecl,
        // the only two other things reaching here (a nested type, alias or
        // static_assert was skipped above). Design §6 treats unmarked private
        // data differently from an unmarked private *function*: the function
        // is silently omitted, the data earns a nudge, and this field is what
        // lets the Tier-A rule (validate.cpp) tell them apart.
        entry.kind = fn != nullptr      ? ir::MemberKind::Function
                     : alias != nullptr ? ir::MemberKind::Alias
                                        : ir::MemberKind::Data;

        // First match wins. The two "has wording" dispositions lead, so a
        // member that produced wording is accounted for however else it is
        // marked -- including a *private* one, which keeps its routed section
        // here even though extract_synopsis drops its synopsis line (design §6
        // deliberately does not special-case a documented private member).
        if (directives.merge) {
            entry.disposition = ir::Disposition::Merged;
        } else if (directives.omit) {
            entry.disposition = ir::Disposition::Omitted;
        } else if (routed_to != routed.end()) {
            // In-class: placed by name. An empty target is the "under no
            // `\ref` group and no `\at`" case -- still routed, just to
            // nothing, which is why it must not read as Described.
            entry.disposition = ir::Disposition::Routed;
            entry.section     = routed_to->second;
        } else if (definition != nullptr && definition->isOutOfLine() && has_docblock(definition, sm)) {
            // Described out of line: the definition carries the markup and
            // lands in its lexically enclosing `\rSec` frame, so there is no
            // routed section to record.
            entry.disposition = ir::Disposition::Described;
        } else if (expos_set.contains(named->getCanonicalDecl())) {
            entry.disposition = ir::Disposition::Expos;
        } else if (real_member.effectively_private) {
            entry.disposition = ir::Disposition::Private;
        } else if (fn != nullptr && (fn->isDefaulted() || fn->isDeleted())) {
            entry.disposition = ir::Disposition::Defaulted;
        } else {
            entry.disposition = ir::Disposition::Undocumented;
        }

        roster.push_back(std::move(entry));
    }

    return roster;
}

// The canonical declarations to drop from every synopsis (design §4.3):
// members whose markup carries `\omit` or `\merge`. Built as a pre-pass because
// a class's synopsis is extracted the moment the class is seen — before the
// out-of-line definition carrying the marker is reached in the event stream —
// so extract_synopsis needs the set up front. Scans both out-of-line
// definitions (keyed to their in-class declaration via getFirstDecl()) and
// in-class members; keys on getCanonicalDecl() so the check in extract_synopsis
// matches whichever redeclaration it walks.
std::set<const clang::Decl*> build_omit_set(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto is_marked = [&sm](const clang::FunctionDecl* fn) {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(fn, sm);
        return dirs.omit || dirs.merge;
    };

    // Out-of-line function definitions, keyed to their in-class declaration.
    std::set<const clang::Decl*> omit =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter(is_marked) |
        std::views::transform([](const clang::FunctionDecl* fn) { return fn->getFirstDecl()->getCanonicalDecl(); }) |
        std::ranges::to<std::set<const clang::Decl*>>();

    // In-class members of a class or class template, including member function
    // templates. The trailing `ranges::to` owns each record's members across
    // the `transform(...) | join` below.
    const auto omit_members_of = [&is_marked](const clang::CXXRecordDecl* record) {
        return record->decls() |
               std::views::transform([](const clang::Decl* member) { return member_function_or_template(member); }) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter(is_marked) |
               std::views::transform([](const clang::FunctionDecl* fn) { return fn->getCanonicalDecl(); }) |
               std::ranges::to<std::vector<const clang::Decl*>>();
    };
    omit.insert_range(decls | std::views::transform(as_record_decl) |
                      std::views::filter([](const clang::CXXRecordDecl* record) {
                          return record != nullptr && record->isThisDeclarationADefinition();
                      }) |
                      std::views::transform(omit_members_of) | std::views::join);

    return omit;
}

// A member's exposition-only name candidate: a data member or method,
// including the underlying method of a FunctionTemplateDecl. Hidden friends
// remain namespace functions rather than class exposition members.
const clang::NamedDecl* as_field_or_method(const clang::Decl* member) {
    if (const auto* fd = llvm::dyn_cast<clang::FieldDecl>(member))
        return fd;
    if (const auto* md = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(member_function_or_template(member)))
        return md;
    return nullptr;
}

// Namespace-owned entities that can be rendered as free-standing exposition
// synopses. isFileContext includes the global namespace and named namespaces,
// while excluding an out-of-line definition of a static data member.
bool is_namespace_expos_candidate(const clang::Decl* decl) {
    if (llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
        llvm::isa<clang::TypeAliasTemplateDecl>(decl))
        return true;
    if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(decl))
        return alias->getDeclContext()->isFileContext();
    const auto* variable = llvm::dyn_cast<clang::VarDecl>(decl);
    return variable != nullptr && variable->getDeclContext()->isFileContext();
}

// The documented-wording counterpart: the same namespace-owned entity kinds,
// asked about by a docblock with description content rather than `\expos` —
// ordinary wording items (design §6) instead of standalone synopses. The
// isFileContext guard excludes the out-of-line definition of a class's own
// member (a static data member, a member variable template), whose wording
// belongs to its class.
const clang::NamedDecl* as_namespace_entity(const clang::Decl* decl) {
    const bool entity_kind = llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
                             llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::TypeAliasDecl>(decl) ||
                             llvm::isa<clang::VarDecl>(decl);
    if (!entity_kind || !decl->getDeclContext()->isFileContext())
        return nullptr;
    return llvm::cast<clang::NamedDecl>(decl);
}

// The exposition-only members (design §4.3/§3.5): canonical member decl →
// its `\exposid` display name (`\expos(name)` override, else `exposid_name` of the
// identifier). A pre-pass like build_omit_set, so extract_synopsis has the set
// when a class is first seen. Covers data members, member functions, and the
// namespace-scope concepts/variables/variable templates/aliases/alias
// templates whose resolved uses are rewritten as exposids
// (extract_namespace_expos_synopsis emits their free-standing declarations).
std::map<const clang::Decl*, std::string> build_expos_set(const std::vector<clang::Decl*>& decls,
                                                          const clang::SourceManager&      sm) {
    const auto is_marked  = [&sm](const clang::NamedDecl* named) { return docblock_directives(named, sm).expos; };
    const auto expos_name = [&sm](const clang::NamedDecl* named) {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(named, sm);
        return dirs.expos_name.value_or(named->getIdentifier() != nullptr
                                            ? beman::specgen::lowering::exposid_name(named->getName().str())
                                            : std::string{});
    };
    // Anonymous records are transparent here: their source members are the
    // candidates, while Clang's implicit injected projections are not.
    const auto expos_members_of = [&](const clang::CXXRecordDecl* record) {
        return real_record_members(record) |
               std::views::transform([](const RealRecordMember& member) { return as_field_or_method(member.decl); }) |
               std::views::filter([](const clang::NamedDecl* named) { return named != nullptr; }) |
               std::views::filter(is_marked) | std::views::transform([&expos_name](const clang::NamedDecl* named) {
                   return std::pair<const clang::Decl*, std::string>{named->getCanonicalDecl(), expos_name(named)};
               }) |
               std::ranges::to<std::vector<std::pair<const clang::Decl*, std::string>>>();
    };

    std::map<const clang::Decl*, std::string> expos;
    expos.insert_range(decls | std::views::transform(as_record_decl) |
                       std::views::filter([](const clang::CXXRecordDecl* record) {
                           return record != nullptr && record->isThisDeclarationADefinition();
                       }) |
                       std::views::transform(expos_members_of) | std::views::join);
    expos.insert_range(
        decls | std::views::filter(is_namespace_expos_candidate) |
        std::views::transform([](const clang::Decl* decl) { return llvm::cast<clang::NamedDecl>(decl); }) |
        std::views::filter(is_marked) | std::views::transform([&expos_name](const clang::NamedDecl* named) {
            return std::pair<const clang::Decl*, std::string>{named->getCanonicalDecl(), expos_name(named)};
        }));
    return expos;
}

// The namespace path enclosing `decl`, most-enclosing first, joined with
// "::" ("" if `decl` is not lexically inside any named namespace). Same
// unfold-then-join shape as qualifier_namespace_name above (this file's
// second "::"-join call site; reuses its exact
// `std::views::join_with(std::string_view("::"))` expression rather than a
// third spelling -- a bare `"::"` literal would drag its NUL terminator into
// the join).
std::string namespace_path(const clang::Decl* decl) {
    std::vector<std::string> parts;
    // substrate generic algorithm: DeclContext parent-chain walk -- an
    // unfold over a linked structure, not a fold over a range that already
    // exists, same shape as qualifier_namespace_name's NestedNameSpecifier
    // walk (step 07).
    for (const clang::DeclContext* ctx = decl->getDeclContext(); ctx != nullptr; ctx = ctx->getParent())
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(ctx); ns != nullptr && !ns->isAnonymousNamespace())
            parts.push_back(ns->getNameAsString());
    if (parts.empty())
        return {};
    std::reverse(parts.begin(), parts.end());
    return parts | std::views::join_with(std::string_view("::")) | std::ranges::to<std::string>();
}

// The namespaces whose qualifiers are dropped from rendered code (design
// §3.5): `std` — the draft writes library names unqualified, being inside
// namespace std — plus every namespace the header itself declares its top-level
// entities in (`demo`, `beman::optional`), which maps onto `std` in the draft
// and so is likewise implicit. Everything else (notably `detail::`) is left
// verbatim for the leakage checker.
//
// "Top-level" is prefix-minimal, and has to be. collect_top_level_decl
// descends through nested namespaces, so the raw paths include `demo::detail`
// — the implementation namespace, whose qualifier this set exists to *keep*.
// Keeping that path would let the two spellings of one qualifier disagree:
// qualifier_namespace_name yields the chain as *written* (`detail`), which
// never equals the fully-qualified path (`demo::detail`), so `detail::storage`
// would stay while `demo::detail::storage` lost its qualifier entirely,
// hiding the leak instead of leaving it visible. Dropping every path that has
// another in the set as a proper prefix makes both spellings render the same
// way, because the prefix walk does the rest: an unmatched `demo::detail`
// chain is descended into, its `demo` prefix matches, and what is left is
// `detail::storage` either way. The same narrowing is what lets a real
// `std::ranges::` qualifier render as the draft writes it, `ranges::`.
std::set<std::string> build_namespace_drop_set(const std::vector<clang::Decl*>& decls) {
    std::set<std::string> paths = decls | std::views::transform(namespace_path) |
                                  std::views::filter([](const std::string& path) { return !path.empty(); }) |
                                  std::ranges::to<std::set<std::string>>();
    paths.insert("std");

    const auto nested_in_another = [&paths](const std::string& path) {
        return std::ranges::any_of(paths, [&path](const std::string& other) {
            return other.size() < path.size() && path.starts_with(other) && path[other.size()] == ':';
        });
    };
    return paths | std::views::filter(std::not_fn(nested_in_another)) | std::ranges::to<std::set<std::string>>();
}

// The complement of QualifierDropper's walk. That visitor answers "does
// this qualifier resolve into the drop set?" and, when it does, deletes it;
// the *no* answers are what design §9's leakage checker needs, and this
// visitor is what keeps them. A qualifier the drop set does not cover is left
// verbatim in rendered code, and a reader of the wording cannot see what
// `detail::` names — so the resolution the dropper already performs is
// exactly the resolution the check needs.
//
// Recorded per *namespace*, not per occurrence: the IR channel is
// document-level (ir::ForeignNamespace) and the validator locates occurrences
// by matching the name against rendered text, the same way its roster half
// locates a leaked member. That makes over-collection harmless — a qualifier
// written only inside a body the tool never renders contributes a name that
// matches nothing — which is why this walks every main-file decl rather than
// threading a second output through the six places QualifierDropper runs.
//
// Two guards keep the finding one-directional (a miss, never a false one):
//
//   - a namespace already in the drop set is skipped even when it is part of a
//     surviving chain, so `demo::detail::storage` reports `detail` alone;
//   - a namespace rooted in `std` is skipped. A `std::ranges::` qualifier is
//     the case this exists for: it survives the drop set (the set holds
//     `std`, not `std::ranges`) and belongs in the draft verbatim, because it
//     is the standard's own vocabulary. That is a property of the namespace's
//     qualified path, not of where it happens to be declared — the guard used
//     to be "declared in the main file", which let the identical rendered
//     `detail::` pass unreported the moment the helper namespace moved into
//     an included implementation header, disabling the leakage guarantee by
//     exactly the refactor the tool's own guidance encourages.
class ForeignQualifierCollector : public clang::RecursiveASTVisitor<ForeignQualifierCollector> {
  public:
    ForeignQualifierCollector(const std::set<std::string>& drop, std::map<std::string, std::string>& out)
        : drop_(drop), out_(out) {}

    bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc nns) {
        if (nns) {
            const std::optional<std::string> written = qualifier_namespace_name(nns.getNestedNameSpecifier());
            if (written && drop_.count(*written) == 0)
                record(nns.getNestedNameSpecifier());
        }
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseNestedNameSpecifierLoc(nns);
    }

    bool TraverseDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr && imported_qualifier_is_droppable(expr->getDecl(), expr->getQualifierLoc(), drop_))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseDeclRefExpr(expr);
    }

    bool TraverseConceptSpecializationExpr(clang::ConceptSpecializationExpr* expr) {
        if (expr != nullptr &&
            imported_qualifier_is_droppable(expr->getNamedConcept(), expr->getNestedNameSpecifierLoc(), drop_))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseConceptSpecializationExpr(expr);
    }

    bool TraverseUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr && expr->getQualifierLoc() && expr->decls_begin() != expr->decls_end() &&
            std::ranges::all_of(expr->decls(), [&](clang::NamedDecl* decl) {
                return imported_qualifier_is_droppable(decl, expr->getQualifierLoc(), drop_);
            }))
            return true;
        return clang::RecursiveASTVisitor<ForeignQualifierCollector>::TraverseUnresolvedLookupExpr(expr);
    }

  private:
    // Each namespace the surviving qualifier writes, innermost first — the
    // same unfold qualifier_namespace_name makes, kept separate because that
    // one joins the chain into the single string the drop set is keyed on and
    // this one needs each link's decl.
    void record(clang::NestedNameSpecifier qualifier) {
        // substrate generic algorithm: the same NestedNameSpecifier unfold
        // qualifier_namespace_name performs (this file, step 07) -- each
        // qualifier yields only its own prefix, so the chain does not exist
        // as a range until the walk produces it.
        while (qualifier.getKind() == clang::NestedNameSpecifier::Kind::Namespace) {
            const clang::NamespaceAndPrefix np    = qualifier.getAsNamespaceAndPrefix();
            const auto*                     named = llvm::dyn_cast_or_null<clang::NamedDecl>(np.Namespace);
            if (named == nullptr)
                return;
            const std::string name      = named->getNameAsString();
            const std::string qualified = named->getQualifiedNameAsString();
            if (!name.empty() && drop_.count(qualified) == 0 && !qualified.starts_with("std::"))
                out_.emplace(name, qualified);
            qualifier = np.Prefix;
        }
    }

    const std::set<std::string>&        drop_;
    std::map<std::string, std::string>& out_;
};

std::vector<beman::specgen::ir::ForeignNamespace> collect_foreign_namespaces(const std::vector<clang::Decl*>& decls,
                                                                             const std::set<std::string>&     drop) {
    // The map deduplicates and orders by name, so the IR channel is stable
    // whichever decl wrote the qualifier first.
    std::map<std::string, std::string> found;
    ForeignQualifierCollector          collector(drop, found);
    // substrate generic algorithm: a for_each-shaped walk driving
    // RecursiveASTVisitor's side effect (appending to `found`), the same
    // shape build_document's collect_top_level_decl loop has -- not a
    // transform building a container from a range.
    for (clang::Decl* decl : decls)
        collector.TraverseDecl(decl);
    return found | std::views::transform([](const auto& kv) {
               return beman::specgen::ir::ForeignNamespace{kv.first, kv.second};
           }) |
           std::ranges::to<std::vector>();
}

// --- what a non-extracted body names (design §9) ----------------------------
//
// Design §9 gives its leakage checker two severities, and the note one is
// about code the tool never prints: an undocumented helper that "appears only
// in non-extracted bodies". Every other validation rule reads something the IR
// already carries or something the front end can add beside it; this one
// cannot, and not by omission — a body with no `\effects-equiv` /
// `\returns-equiv` marker is *by construction* the code that never becomes
// wording, so no node holds it and no amount of Tier-A reading finds it. What
// the front end records here is exactly that absence: the class members such a
// body names, for a validator that then asks the roster whether the reader
// could have seen them.
//
// The members a function body refers to, by name. Resolved references only —
// a `MemberExpr` or a `DeclRefExpr` whose declaration's parent is a class
// declared in the main file — which is narrower than scanning the body's text
// two ways that both matter. It excludes a local, a parameter and a `std::`
// name, none of which a roster ever holds; and it excludes a *dependent*
// member access (`this->helper()` through a dependent base), whose name
// resolves to no declaration here. The first exclusion is what keeps the
// Tier-A rule from reporting a body-local `tmp` that happens to share a name
// with a hidden member; the second is a miss, and is the direction every
// leakage rule errs in (a miss costs a finding, never invents one).
class BodyMemberCollector : public clang::RecursiveASTVisitor<BodyMemberCollector> {
  public:
    BodyMemberCollector(const clang::SourceManager& sm, std::set<std::string>& out) : sm_(sm), out_(out) {}

    bool VisitMemberExpr(clang::MemberExpr* expr) {
        record(expr->getMemberDecl());
        return true;
    }

    // A static member function or static data member, and an unqualified call
    // to a member of the current instantiation that Sema resolved without an
    // implicit object argument, arrive as a plain reference rather than a
    // member access.
    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        record(expr->getDecl());
        return true;
    }

  private:
    void record(const clang::NamedDecl* decl) {
        if (decl == nullptr)
            return;
        const auto* parent = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(decl->getDeclContext());
        if (parent == nullptr || !sm_.isInMainFile(parent->getLocation()))
            return;
        const std::string name = decl->getNameAsString();
        if (!name.empty())
            out_.insert(name);
    }

    const clang::SourceManager& sm_;
    std::set<std::string>&      out_;
};

// Does `fn` carry wording whose body the tool leaves unrendered? Three
// conditions, each excluding a body whose contents are somebody else's
// question:
//
//   - it has a docblock: an undocumented function's body specifies nothing,
//     so what it reaches for is an implementation detail all the way down;
//   - the docblock does not say `\omit`/`\merge`: the same, one step later —
//     the entity is declared but deliberately unspecified;
//   - the docblock carries no extraction marker: an extracted body *is*
//     wording, so a hidden name in it is design §9's **error** case, which
//     the leakage checker reports off the rendered `EquivalentTo` text.
bool has_unextracted_body(const clang::FunctionDecl* fn, const clang::SourceManager& sm) {
    if (fn == nullptr || !fn->isThisDeclarationADefinition() || fn->getBody() == nullptr)
        return false;
    const clang::RawComment* rc = fn->getASTContext().getRawCommentForDeclNoCache(fn);
    if (rc == nullptr)
        return false;
    const llvm::StringRef raw = rc->getRawText(sm);
    if (!is_docblock_comment(raw))
        return false;

    const beman::specgen::lowering::Lowered lowered =
        beman::specgen::lowering::lower(beman::specgen::grammar::parse_docblock(raw.str()).block);
    if (lowered.directives.omit || lowered.directives.merge)
        return false;
    return std::ranges::none_of(lowered.descr.elements, [](const beman::specgen::ir::DescriptionElement& element) {
        return element.equivalent.has_value();
    });
}

// How a finding names the body it is about: `<class>::<name>` for a member,
// the bare name for a free function. Not a qualified name — the enclosing
// namespace maps onto `std` in the draft (design §3.5), so printing it would
// put a namespace in a diagnostic that the rendered wording deliberately does
// not have. This is the same "which class is this about" answer
// `ir::Synopsis::name` gives, spelled for a member instead of a type.
std::string body_owner_label(const clang::FunctionDecl* fn) {
    const clang::CXXRecordDecl* record = enclosing_record(fn);
    if (record == nullptr)
        return fn->getNameAsString();
    // A constructor's own name is the *injected class name*, which for a
    // class template carries the parameter list -- `optional<T>` -- and would
    // print as `optional::optional<T>`. The class already stands to the left
    // of the `::`, so the plain record name is both shorter and what a reader
    // writing the header saw.
    const std::string name =
        llvm::isa<clang::CXXConstructorDecl>(fn) ? record->getNameAsString() : fn->getNameAsString();
    return record->getNameAsString() + "::" + name;
}

// Every (documented function with an unrendered body, member it names) pair in
// the header. Both halves of the same two-part walk every marker pre-pass
// makes — out-of-line definitions from `decls`, in-class definitions and
// hidden friends from each record's members — since a body is a body wherever
// it is written.
std::vector<beman::specgen::ir::BodyUse> collect_unextracted_uses(const std::vector<clang::Decl*>& decls,
                                                                  const clang::SourceManager&      sm) {
    // One body's members, already paired with the label that names it. Pairs
    // rather than BodyUses so the set below can order them without a
    // comparator; the two are the same two strings.
    const auto uses_of = [&sm](const clang::FunctionDecl* fn) {
        std::set<std::string> names;
        BodyMemberCollector   collector(sm, names);
        collector.TraverseStmt(fn->getBody());
        const std::string owner = body_owner_label(fn);
        return names | std::views::transform([owner](const std::string& member) { return std::pair{owner, member}; }) |
               std::ranges::to<std::vector>();
    };

    const auto bodies = [&sm](const clang::FunctionDecl* fn) { return has_unextracted_body(fn, sm); };

    // Out-of-line function definitions.
    std::vector<const clang::FunctionDecl*> functions = decls | std::views::transform(as_out_of_line_function) |
                                                        std::views::filter(bodies) |
                                                        std::ranges::to<std::vector<const clang::FunctionDecl*>>();

    // In-class definitions and hidden friends, the shape collect_inclass_items
    // attaches wording from.
    functions.append_range(decls | std::views::transform(as_record_decl) |
                           std::views::filter([](const clang::CXXRecordDecl* record) {
                               return record != nullptr && record->isThisDeclarationADefinition();
                           }) |
                           std::views::transform([](const clang::CXXRecordDecl* record) {
                               return record->decls() | std::views::transform(member_function_or_template) |
                                      std::ranges::to<std::vector<const clang::FunctionDecl*>>();
                           }) |
                           std::views::join | std::views::filter(bodies));

    // The set deduplicates and orders, so the IR channel does not depend on
    // which decl reached a member first — two constructors sharing an owner
    // label and a helper contribute one entry, which is also the finding a
    // reader wants (this class's wording leans on that helper).
    const std::set<std::pair<std::string, std::string>> found =
        functions | std::views::transform(uses_of) | std::views::join |
        std::ranges::to<std::set<std::pair<std::string, std::string>>>();

    return found | std::views::transform([](const auto& pair) {
               return beman::specgen::ir::BodyUse{pair.first, pair.second};
           }) |
           std::ranges::to<std::vector>();
}

// The function operands the synopsis/itemdecl renders as `\seebelow`
// (design §4.3): canonical decls map to the selected operand.
// Same pre-pass
// shape as build_omit_set (out-of-line defs keyed to getFirstDecl(), in-class
// members to themselves), so extract_synopsis has the set when a class is seen.
SeeBelowMap build_seebelow_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto marked_target = [&sm](const clang::FunctionDecl* fn) {
        return seebelow_target(docblock_directives(fn, sm));
    };

    // Out-of-line function definitions, keyed to their in-class declaration.
    SeeBelowMap result =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter([&](const clang::FunctionDecl* fn) { return marked_target(fn).has_value(); }) |
        std::views::transform([&](const clang::FunctionDecl* fn) {
            return std::pair<const clang::Decl*, SeeBelowTarget>{fn->getFirstDecl()->getCanonicalDecl(),
                                                                 *marked_target(fn)};
        }) |
        std::ranges::to<SeeBelowMap>();

    // In-class members of a class or class template: member_as_function_or_friend's
    // FunctionDecl-or-FriendDecl unwrap, plus (unlike build_omit_set) a plain
    // in-class function template -- \seebelow can mark one, and unlike a
    // friend target this is not ambiguous with anything build_omit_set already
    // recognizes, so it is a local addition rather than a change to the shared
    // projection.
    const auto seebelow_members_of = [&marked_target](const clang::CXXRecordDecl* record) {
        const std::vector<const clang::Decl*> members(record->decls_begin(), record->decls_end());
        return members | std::views::transform([](const clang::Decl* member) -> const clang::FunctionDecl* {
                   if (const clang::FunctionDecl* fn = member_as_function_or_friend(member))
                       return fn;
                   if (const auto* ft = llvm::dyn_cast<clang::FunctionTemplateDecl>(member))
                       return ft->getTemplatedDecl();
                   return nullptr;
               }) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter([&](const clang::FunctionDecl* fn) { return marked_target(fn).has_value(); }) |
               std::views::transform([&](const clang::FunctionDecl* fn) {
                   return std::pair<const clang::Decl*, SeeBelowTarget>{fn->getCanonicalDecl(), *marked_target(fn)};
               }) |
               std::ranges::to<std::vector<std::pair<const clang::Decl*, SeeBelowTarget>>>();
    };
    result.insert_range(decls | std::views::transform(as_record_decl) |
                        std::views::filter([](const clang::CXXRecordDecl* record) {
                            return record != nullptr && record->isThisDeclarationADefinition();
                        }) |
                        std::views::transform(seebelow_members_of) | std::views::join);

    return result;
}

// The literal draft synopsis suffix requested by \freestanding or
// \freestanding-deleted. Like SeeBelowMap, this is transient front-end
// metadata keyed to the canonical in-class declaration: an out-of-line
// definition commonly carries the markup, but the earlier class synopsis is
// where the comment must be emitted.
FreestandingMap build_freestanding_map(const std::vector<clang::Decl*>& decls, const clang::SourceManager& sm) {
    const auto marked_kind = [&sm](const clang::FunctionDecl* fn) -> std::optional<FreestandingKind> {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(fn, sm);
        if (dirs.freestanding_deleted)
            return FreestandingKind::Deleted;
        if (dirs.freestanding)
            return FreestandingKind::Freestanding;
        return std::nullopt;
    };

    FreestandingMap result =
        decls | std::views::transform(as_out_of_line_function) | std::views::filter([](const clang::FunctionDecl* fn) {
            return fn != nullptr && fn->isThisDeclarationADefinition() && fn->isOutOfLine();
        }) |
        std::views::filter([&](const clang::FunctionDecl* fn) { return marked_kind(fn).has_value(); }) |
        std::views::transform([&](const clang::FunctionDecl* fn) {
            return std::pair<const clang::Decl*, FreestandingKind>{fn->getFirstDecl()->getCanonicalDecl(),
                                                                   *marked_kind(fn)};
        }) |
        std::ranges::to<FreestandingMap>();

    const auto marked_members_of = [&marked_kind](const clang::CXXRecordDecl* record) {
        const std::vector<const clang::Decl*> members(record->decls_begin(), record->decls_end());
        return members | std::views::transform(member_function_or_template) |
               std::views::filter([](const clang::FunctionDecl* fn) { return fn != nullptr; }) |
               std::views::filter([&](const clang::FunctionDecl* fn) { return marked_kind(fn).has_value(); }) |
               std::views::transform([&](const clang::FunctionDecl* fn) {
                   return std::pair<const clang::Decl*, FreestandingKind>{fn->getCanonicalDecl(), *marked_kind(fn)};
               }) |
               std::ranges::to<std::vector<std::pair<const clang::Decl*, FreestandingKind>>>();
    };
    result.insert_range(decls | std::views::transform(as_record_decl) |
                        std::views::filter([](const clang::CXXRecordDecl* record) {
                            return record != nullptr && record->isThisDeclarationADefinition();
                        }) |
                        std::views::transform(marked_members_of) | std::views::join);
    return result;
}

// --- stage 1: classify() ----------------------------------------------------
//
// classify() converts one RawItem into a document_build::DocEvent (decision
// document-build-stages' closed, clang-free variant): the dyn_cast if/else
// chain over clang::Decl
// subclasses is inherently Tier B and is quarantined here — this is the only
// function downstream of RawItem collection that still touches a
// clang::Decl*. build_document() calls it once per RawItem and hands the
// resulting events to document_build::build_tree, clang-free from here on
// (see include/beman/specgen/document_build.hpp, including its top-of-file
// note on why \also/empty-descr grouping is decided partly here and partly
// inside build_tree rather than as a later pass over the finished tree).
namespace db = beman::specgen::document_build;

// Defined alongside parse_rsec's other helpers, below (needs forward
// declaring here since classify() -- textually earlier in the file -- is its
// only caller). See its definition for what it does and why.
bool rsec_tag_recognized(std::string_view raw);

struct UnrecognizedSectionHeader {
    std::string stable_name;
    unsigned    line_offset = 0;
};

std::optional<UnrecognizedSectionHeader> unrecognized_section_header(std::string_view raw);

// Fold one attached item into its DocEvent: classify()'s shared tail for
// every top-level decl that becomes an ItemDecl — an out-of-line or free
// function definition, or a documented record declaration the header never
// defines. Kept out of classify() so both arms make the same
// `\omit`/`\merge` and grouping decisions rather than drifting apart.
beman::specgen::document_build::DocEvent item_decl_event(AttachedItem&& attached) {
    namespace db = beman::specgen::document_build;

    if (attached.directives.omit || attached.directives.merge)
        // \omit / \merge (design §4.3): no itemdescr. Removal from the
        // synopsis is handled by the omit-set pre-pass (build_omit_set +
        // extract_synopsis), which ran before this decl was reached. The
        // docblock's own findings still travel: the entity is
        // unspecified deliberately, but a malformed marker in the block that
        // says so is not deliberate.
        return db::Ignored{std::move(attached.diagnostics)};

    // This is only the "does this item *want* to join" half of \also/
    // empty-descr grouping — a property of this item alone. Whether a join
    // actually happens depends on tree-adjacency context this function does
    // not have (is there a preceding primary in the same open \rSec frame,
    // in push order); that half belongs to document_build::build_tree, which
    // evaluates it before this item's descr is touched by anything, so a
    // follower that turns out to have no primary keeps its own content
    // (design §4.3; see document_build.hpp's top-of-file note for the two
    // review findings this split fixes: a follower's content must not be
    // discarded just because it *asked* to join, and the join must be
    // decided in push order, not the placement-key-sorted order the tree
    // ends up in).
    const bool named_grouping = attached.directives.group_id || attached.directives.also_target;
    const bool wants_join     = !named_grouping && (attached.directives.also || attached.item.descr.elements.empty());

    return db::ItemDecl{attached.inclass_offset,
                        wants_join,
                        std::move(attached.item),
                        std::move(attached.diagnostics),
                        std::move(attached.directives.group_id),
                        std::move(attached.directives.also_target),
                        attached.grouping_line};
}

// A record declaration that is not a definition. Three cases, none of which
// is the empty Synopsis node this used to produce (whose rendering was an
// empty code block — worse than nothing in a paper):
//
//  - The entity is defined elsewhere: the definition's own event carries the
//    synopsis, and this redeclaration is plumbing. No node, silent.
//  - Documented and never defined: an undefined class-template primary (or
//    plain record) whose declaration *is* the wording — an ordinary itemdecl
//    plus the docblock's description, through item_decl_event so
//    `\omit`/`\merge` and `\also` grouping mean what they mean everywhere
//    else.
//  - Undocumented and never defined: omitted, silent — the same treatment
//    every unmarked entity gets (design §6).
beman::specgen::document_build::DocEvent
classify_record_declaration(const clang::NamedDecl*                          decl,
                            const clang::CXXRecordDecl*                      record,
                            const clang::SourceManager&                      sm,
                            const clang::LangOptions&                        lang_opts,
                            const std::set<std::string>&                     ns_drop_set,
                            const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace db = beman::specgen::document_build;

    if (record == nullptr || record->hasDefinition())
        return db::Ignored{};
    if (!has_docblock(decl, sm))
        return db::Ignored{};
    return item_decl_event(attach_record_declaration(decl, record, sm, lang_opts, ns_drop_set, expos_set));
}

db::DocEvent classify(const RawItem&                                   ev,
                      const clang::SourceManager&                      sm,
                      const clang::LangOptions&                        lang_opts,
                      const std::set<const clang::Decl*>&              omit_set,
                      const std::map<const clang::Decl*, std::string>& expos_set,
                      const SeeBelowMap&                               seebelow_map,
                      const FreestandingMap&                           freestanding_map,
                      const std::set<std::string>&                     ns_drop_set,
                      const SkippedRanges&                             skipped) {
    namespace ir = beman::specgen::ir;

    if (ev.decl == nullptr) {
        if (ev.comment_text.find("\\verbatim-synopsis") != std::string::npos ||
            ev.comment_text.find("\\verbatim-itemdecl") != std::string::npos) {
            const std::optional<std::size_t> start = docblock_start(ev.comment_text);
            if (start) {
                const std::string_view markup     = std::string_view(ev.comment_text).substr(*start);
                grammar::ParseResult   parsed     = grammar::parse_docblock(markup);
                const unsigned         first_line = sm.getLineNumber(sm.getMainFileID(), ev.offset) +
                                                    static_cast<unsigned>(std::ranges::count(
                                                        std::string_view(ev.comment_text).substr(0, *start), '\n'));
                std::vector<db::Diagnostic> diagnostics =
                    parsed.diags | std::views::transform([first_line](const grammar::Diagnostic& diagnostic) {
                        const unsigned line =
                            diagnostic.line > 0 ? first_line + static_cast<unsigned>(diagnostic.line) - 1 : first_line;
                        return db::Diagnostic{diagnostic.severity, line, diagnostic.message};
                    }) |
                    std::ranges::to<std::vector<db::Diagnostic>>();

                if (parsed.block.verbatim_itemdecl) {
                    lowering::Lowered lowered = lowering::lower(parsed.block);
                    ir::SpecItem      item;
                    item.decl.signatures.push_back(ir::CodeText{std::move(*parsed.block.verbatim_itemdecl), {}});
                    item.descr = std::move(lowered.descr);
                    return db::ItemDecl{ev.offset, false, std::move(item), std::move(diagnostics)};
                }
                if (parsed.block.markers.verbatim_synopsis) {
                    db::SynopsisDecl out;
                    out.offset        = ev.offset;
                    out.synopsis.code = ir::CodeText{parsed.block.verbatim_synopsis.value_or(""), {}};
                    out.diagnostics   = std::move(diagnostics);
                    return out;
                }
            }
        }

        const parse::parse_result<SectionHeader> header = parse_rsec(ev.comment_text);
        if (!header) {
            if (rsec_tag_recognized(ev.comment_text)) {
                // The \rSec tag itself matched; the failure is in the
                // depth/[stable]/{title} grammar after it -- a malformed
                // \rSec. The comment still contributes no node (design
                // §3.2), but the fact is reported rather than silently
                // dropped. Two positions
                // are worth having: Diagnostic::offset locates the comment in
                // the file (what the driver prints as the location), while
                // the parse failure's own offset locates the bad character
                // inside it.
                const parse::parse_error& failure = header.error();
                // The comment's own line in the main file: RawItem carries a
                // byte offset (its place in the interleave), and the line is
                // what the driver prints.
                return db::Ignored{{db::Diagnostic{beman::specgen::Severity::Warning,
                                                   sm.getLineNumber(sm.getMainFileID(), ev.offset),
                                                   std::format("malformed \\rSec marker: {} (comment offset {})",
                                                               failure.message,
                                                               failure.where.offset)}}};
            }
            if (const auto candidate = unrecognized_section_header(ev.comment_text)) {
                return db::Ignored{
                    {db::Diagnostic{beman::specgen::Severity::Warning,
                                    sm.getLineNumber(sm.getMainFileID(), ev.offset) + candidate->line_offset,
                                    std::format("unrecognized section header [{}]; use \\rSec<depth>[{}]{{title}}",
                                                candidate->stable_name,
                                                candidate->stable_name)}}};
            }
            return db::Ignored{}; // \ref group headers, license/SPDX text, or
                                  // trailing braces (design §3.2): none of
                                  // these are structure, and none are errors.
        }
        return db::SectionOpen{ev.offset, header->value.depth, header->value.stable, header->value.title};
    }

    // A marked namespace-scope concept or variable is a
    // complete free-standing synopsis. The source spelling remains its IR
    // name; only the rendered declaration identifier takes the exposid name.
    const clang::NamedDecl* namespace_expos = nullptr;
    if (is_namespace_expos_candidate(ev.decl))
        namespace_expos = llvm::cast<clang::NamedDecl>(ev.decl);
    if (namespace_expos != nullptr && expos_set.contains(namespace_expos->getCanonicalDecl())) {
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = namespace_expos->getNameAsString();
        out.synopsis.code = extract_namespace_expos_synopsis(namespace_expos, sm, lang_opts, ns_drop_set, expos_set);
        return out;
    }

    // A documented namespace-scope alias (template), variable (template), or
    // concept is an ordinary wording item (design §6): the declaration
    // itself is the itemdecl and the docblock's description follows, the
    // way [concept.same] and [tuple.helper] write them. Marked `\expos` it
    // took the standalone-synopsis arm above instead (every marked kind here
    // is an expos candidate); undocumented it stays absent, like every other
    // unannotated entity. Aliases route through attach_alias, whose masking
    // rules (`\seebelow`/`\impdef`) and grouping apply at namespace scope
    // exactly as they do in a class body.
    if (const clang::NamedDecl* entity = as_namespace_entity(ev.decl);
        entity != nullptr && has_docblock(ev.decl, sm)) {
        if (const auto* alias_tmpl = llvm::dyn_cast<clang::TypeAliasTemplateDecl>(entity))
            return item_decl_event(
                attach_alias(alias_tmpl->getTemplatedDecl(), sm, lang_opts, ns_drop_set, expos_set, alias_tmpl));
        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(entity))
            return item_decl_event(attach_alias(alias, sm, lang_opts, ns_drop_set, expos_set));
        return item_decl_event(attach_namespace_entity(entity, sm, lang_opts, ns_drop_set, expos_set));
    }

    // A class/struct/union definition heads a synopsis; every other top-level
    // decl (free function, out-of-line member definition, variable, ...) is a
    // spec item. A defined class gets its synopsis text extracted (design
    // §3.4); a declaration that defines nothing goes through
    // classify_record_declaration — an itemdecl when it is a documented
    // undefined primary, no node at all otherwise, and never an empty
    // Synopsis (which rendered as an empty code block).
    //
    // A class template is not itself a CXXRecordDecl — top-level iteration
    // yields the ClassTemplateDecl, whose templated decl (getTemplatedDecl())
    // is the CXXRecordDecl carrying the actual members — so it needs its own
    // arm or it would fall through to SpecItem and lose the whole class.
    // extract_synopsis takes the ClassTemplateDecl as the extraction range's
    // head so the synopsis starts at `template`, not at `class`.
    if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(ev.decl)) {
        if (!record->isThisDeclarationADefinition())
            return classify_record_declaration(record, record, sm, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(record, sm))
            return db::Ignored{std::move(*diagnostics)};
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = record->getNameAsString();
        out.synopsis.code =
            extract_synopsis(record, sm, lang_opts, omit_set, expos_set, seebelow_map, freestanding_map, ns_drop_set);
        collect_inclass_items(record, sm, lang_opts, ns_drop_set, expos_set, skipped, out.pending, out.diagnostics);
        // After collection: the roster reads the routed sections
        // collect_inclass_items just decided.
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(record, sm, expos_set, out.pending);
        group_adjacent_aliases(out.pending);
        out.general = derive_class_mandates(record, sm, lang_opts, ns_drop_set, expos_set);
        return out;
    }
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(ev.decl)) {
        const clang::CXXRecordDecl* templated = tmpl->getTemplatedDecl();
        if (templated == nullptr || !templated->isThisDeclarationADefinition())
            return classify_record_declaration(tmpl, templated, sm, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(tmpl, sm))
            return db::Ignored{std::move(*diagnostics)};
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = templated->getNameAsString();
        out.synopsis.code = extract_synopsis(
            templated, sm, lang_opts, omit_set, expos_set, seebelow_map, freestanding_map, ns_drop_set, tmpl);
        collect_inclass_items(templated, sm, lang_opts, ns_drop_set, expos_set, skipped, out.pending, out.diagnostics);
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(templated, sm, expos_set, out.pending);
        group_adjacent_aliases(out.pending);
        out.general = derive_class_mandates(templated, sm, lang_opts, ns_drop_set, expos_set);
        return out;
    }

    // \also/\omit (design §4.3, Model A — see build_spec_item): an omitted
    // function definition contributes no node at all; a described overload
    // starts a group and an \also/empty-descr follower joins its signature
    // onto the group's primary instead of starting a new SpecItem. Only
    // function-definition items participate — a non-function decl (empty
    // SpecItem, is_function_def == false) can neither join nor act as a group
    // primary.
    AttachedItem attached = build_spec_item(ev.decl, sm, lang_opts, ns_drop_set, expos_set, skipped);
    if (!attached.is_function_def) {
        // A top-level decl no arm above turned into a node. Undocumented,
        // that is the ordinary fate of an unannotated entity (design §6),
        // and it stays silent. *Documented*, it means authored wording is
        // about to be dropped, and dropping it silently is the failure mode
        // a vocabulary header suffers worst — the header validates clean
        // while its descriptions vanish. An `\omit`/`\merge`/`\expos` marker
        // is the author asking for no itemdescr; anything else is an Error
        // naming the kind, so an unsupported entity kind is loud the first
        // time someone documents one.
        if (has_docblock(ev.decl, sm)) {
            const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(ev.decl, sm);
            if (!dirs.omit && !dirs.merge && !dirs.expos) {
                const unsigned line = sm.getLineNumber(sm.getMainFileID(), ev.offset);
                // A documented function *declaration* is the one shape here
                // with a better answer than "unsupported": the markup
                // belongs at the definition, which is what places a
                // function's wording (design §3.3). A deduction guide is a
                // FunctionDecl that can never have one, so it stays with the
                // unsupported-kind report.
                const clang::FunctionDecl* fn = as_out_of_line_function(ev.decl);
                std::string message = fn != nullptr && !llvm::isa<clang::CXXDeductionGuideDecl>(fn)
                                          ? "a documented function declaration produces no wording; markup belongs at "
                                            "the definition, which places a function's wording (design §3.3)"
                                          : std::format("a documented {} produces no wording: unsupported entity kind",
                                                        ev.decl->getDeclKindName());
                return db::Ignored{{db::Diagnostic{beman::specgen::Severity::Error, line, std::move(message)}}};
            }
        }
        return db::Ignored{};
    }
    return item_decl_event(std::move(attached));
}

} // namespace

namespace {

// Helper parsers for parse_rsec, below (decision parser-combinators). Not
// promoted to foundation/parse/parser.hpp: nothing else needs a `\rSec`
// grammar's blank-skipping or delimiter-content shape yet.

// Skips leading horizontal whitespace (space/tab only). Unlike
// foundation/parse/cursor.hpp's
// skip_intertoken_space(), this does not cross a newline — a `\rSec` marker
// is scanned within a single raw comment line, so a later line of a
// multi-line raw comment must never read as more leading whitespace before
// the tag.
constexpr auto skip_blank(parse::cursor cur) -> parse::cursor {
    while (!cur.empty() && (cur.peek() == ' ' || cur.peek() == '\t')) // substrate generic algorithm
        cur = cur.bump();
    return cur;
}

// A parser that always succeeds, skipping leading blanks and discarding its
// own value — the sequence_right glue at parse_rsec's four blank-tolerant
// points (before the decoration, before the tag,
// before '[', before '{').
[[nodiscard]] auto blanks() {
    return parse::parser{[](parse::cursor cur) -> parse::parse_result<bool> {
        return parse::parse_state<bool>{true, skip_blank(cur)};
    }};
}

// "//", "///", or "//!": the three raw-comment decorations
// RawComment::getRawText() may hand back. Always consumes the shared "//"
// first, then greedily consumes one more '/' or '!' if present — equivalent
// to trying the longest decoration first, since no value depends on which
// one matched.
[[nodiscard]] auto decoration() {
    return parse::sequence_right(parse::keyword("//"), parse::opt(parse::char_p('/') | parse::char_p('!')));
}

// Collects the run of characters strictly between an `open`/`close` pair,
// discarding the delimiters — the shared shape behind both `[stable]` and
// `{title}`. The run may be empty (e.g. `[]`); an unterminated run (no
// `close` before the input ends) is a positioned failure at end-of-input
// rather than a silent non-match.
[[nodiscard]] auto bracketed(char open, char close, const char* open_message, const char* close_message) {
    return parse::map(parse::sequence_right(
                          parse::satisfy([open](char c) { return c == open; }, open_message),
                          parse::sequence_left(
                              parse::many(parse::satisfy([close](char c) { return c != close; }, "delimiter content")),
                              parse::satisfy([close](char c) { return c == close; }, close_message))),
                      [](std::vector<char> chars) { return std::string(chars.begin(), chars.end()); });
}

// The `\rSec` tag alone -- decoration, blanks, and the literal "\rSec"
// keyword -- with no depth/[stable]/{title} grammar attached. parse_rsec,
// below, runs this as its own first step; rsec_tag_recognized() runs
// it again on its own so classify() can ask "did this comment even look
// like \rSec" independent of whether the rest of the marker goes on to
// parse, without duplicating the grammar by hand.
[[nodiscard]] auto rsec_tag() {
    return parse::sequence_right(
        blanks(), parse::sequence_right(decoration(), parse::sequence_right(blanks(), parse::keyword("\\rSec"))));
}

// True once `raw`'s \rSec tag
// itself has matched, independent of whether the depth/[stable]/{title}
// grammar after it goes on to parse. classify() calls this only when
// parse_rsec(raw) has already failed, to tell apart the two ways that can
// happen: the tag itself never matched (this returns false -- "not a \rSec
// at all", frontend.hpp's parse_rsec doc comment's non-match case, e.g. a
// `\ref` group header sharing \rSec's leading "\r" isn't enough to make this
// true, since the keyword comparison itself still fails), or the tag matched
// and something after it did not (this returns true -- a malformed \rSec).
// This is the tag's own success/failure, the same commit-on-consumed-input
// boundary parse_rsec's combinators already apply internally -- not a
// re-derivation of the distinction by some other means, such as comparing
// parse_error offsets against a guessed constant, which a marker sharing a
// literal prefix with "\rSec" (like `\ref`) would get wrong.
[[nodiscard]] bool rsec_tag_recognized(std::string_view raw) { return rsec_tag()(parse::cursor{raw}).has_value(); }

// Recognize the other section-heading convention real headers use,
// `// 22.5.3.3 Destructor[optional.dtor]`. It cannot open a section because it
// carries no depth, but silently treating it as ordinary prose files every
// following declaration into the previous section.
//
// This is intentionally a warning heuristic, not a second section grammar.
// Requiring numbered heading text and a final multi-component dotted name
// avoids ordinary prose references (`[structure.specifications]`), synopsis
// group headers (`\ref{...}`), trailing references, and Doxygen's
// `/// END [optional.syn]`. RawComment merges consecutive `//` lines, so scan
// each physical line and retain its offset for the diagnostic location.
std::optional<UnrecognizedSectionHeader> unrecognized_section_header(std::string_view raw) {
    const auto is_name_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '+';
    };
    const auto is_dotted_name = [&](std::string_view name) {
        return name.contains('.') && name.front() != '.' && name.back() != '.' && !name.contains("..") &&
               std::ranges::all_of(name, [&](char c) { return c == '.' || is_name_char(c); });
    };

    std::size_t line_begin  = 0;
    unsigned    line_offset = 0;
    // substrate generic algorithm: scanning preserves both the byte position
    // in the merged RawComment and the physical-line offset used by the
    // diagnostic; views::split would discard the first and require a second
    // stateful pass to recover the latter.
    while (line_begin <= raw.size()) {
        const std::size_t newline  = raw.find('\n', line_begin);
        const std::size_t line_end = newline == std::string_view::npos ? raw.size() : newline;
        std::string_view  line     = raw.substr(line_begin, line_end - line_begin);

        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos) {
            line.remove_prefix(first);
            if (line_vocabulary(llvm::StringRef{line}) == CommentVocabulary::Draft && line.starts_with("//")) {
                line.remove_prefix(2);
                const std::size_t body_begin = line.find_first_not_of(" \t");
                if (body_begin != std::string_view::npos) {
                    line.remove_prefix(body_begin);
                    const std::size_t body_end = line.find_last_not_of(" \t\r");
                    line                       = line.substr(0, body_end + 1);
                    const std::size_t open     = line.rfind('[');
                    if (open != std::string_view::npos && line.ends_with(']') && line.front() >= '0' &&
                        line.front() <= '9') {
                        const std::string_view stable = line.substr(open + 1, line.size() - open - 2);
                        if (is_dotted_name(stable))
                            return UnrecognizedSectionHeader{std::string(stable), line_offset};
                    }
                }
            }
        }

        if (newline == std::string_view::npos)
            break;
        line_begin = newline + 1;
        ++line_offset;
    }
    return std::nullopt;
}

} // namespace

parse::parse_result<SectionHeader> parse_rsec(std::string_view raw) {
    // \rSec<depth>[<stable>]{<title>}, tolerant of "//"/"///"/"//!"
    // decoration and horizontal whitespace at the points blanks() marks
    // below — never inside the brackets/braces, matching the grammar
    // classify() accepts. rsec_tag() is this same tag,
    // factored out so rsec_tag_recognized() can run it on its own.
    auto depth = parse::sequence_right(rsec_tag(), parse::digits());

    auto depth_and_stable =
        parse::lift2(depth,
                     parse::sequence_right(blanks(), bracketed('[', ']', "expected '['", "expected ']'")),
                     [](int d, std::string stable) { return std::pair{d, std::move(stable)}; });

    auto header =
        parse::lift2(depth_and_stable,
                     parse::sequence_right(blanks(), bracketed('{', '}', "expected '{'", "expected '}'")),
                     [](std::pair<int, std::string> depth_stable, std::string title) {
                         return SectionHeader{depth_stable.first, std::move(depth_stable.second), std::move(title)};
                     });

    return header(parse::cursor{raw});
}

namespace {

// Clang coalesces adjacent line comments into one RawComment, but each
// physical \rSec line is a distinct structure event. Split only around those
// lines: the intervening chunks stay intact so multi-line docblocks and other
// comment consumers retain their existing input shape. Offsets remain file
// offsets rather than offsets into the merged RawComment.
void append_rsec_comment_items(std::vector<RawItem>& out, unsigned raw_begin, std::string raw) {
    const std::size_t first = raw.find_first_not_of(" \t");
    if (first == std::string::npos || !std::string_view(raw).substr(first).starts_with("//")) {
        out.push_back(RawItem{raw_begin, nullptr, std::move(raw)});
        return;
    }

    std::size_t chunk_begin = 0;
    std::size_t line_begin  = 0;
    // substrate generic algorithm: a source-position-preserving partition at
    // structural lines; views::split would discard the byte offsets needed to
    // put the resulting events and diagnostics at their physical locations.
    while (line_begin < raw.size()) {
        const std::size_t      newline  = raw.find('\n', line_begin);
        const std::size_t      line_end = newline == std::string::npos ? raw.size() : newline;
        const std::string_view line(raw.data() + line_begin, line_end - line_begin);
        if (rsec_tag_recognized(line)) {
            if (chunk_begin < line_begin) {
                out.push_back(RawItem{raw_begin + static_cast<unsigned>(chunk_begin),
                                      nullptr,
                                      raw.substr(chunk_begin, line_begin - chunk_begin)});
            }

            const std::size_t after_line = newline == std::string::npos ? raw.size() : newline + 1;
            out.push_back(RawItem{raw_begin + static_cast<unsigned>(line_begin),
                                  nullptr,
                                  raw.substr(line_begin, after_line - line_begin)});
            chunk_begin = after_line;
        }

        if (newline == std::string::npos)
            break;
        line_begin = newline + 1;
    }

    if (chunk_begin < raw.size())
        out.push_back(RawItem{raw_begin + static_cast<unsigned>(chunk_begin), nullptr, raw.substr(chunk_begin)});
}

struct HeaderSynopsisEnd {
    std::string stable;
    std::size_t line_begin = 0;
};

// The header-synopsis closing fence is deliberately Doxygen, not a specgen marker:
// it bounds the source region without becoming authored wording. Clang can
// merge it into the RawComment that carries a terminal \verbatim-synopsis, so
// return byte offsets as well as the stable name.
std::optional<HeaderSynopsisEnd> header_synopsis_end(std::string_view raw) {
    std::size_t begin = 0;
    // substrate generic algorithm: preserve byte offsets while scanning a
    // possibly merged RawComment for the exact physical fence line.
    while (begin <= raw.size()) {
        const std::size_t newline = raw.find('\n', begin);
        const std::size_t end     = newline == std::string_view::npos ? raw.size() : newline;
        std::string_view  text    = raw.substr(begin, end - begin);
        const std::size_t first   = text.find_first_not_of(" \t");
        if (first != std::string_view::npos) {
            text.remove_prefix(first);
            if (text.starts_with("///")) {
                text.remove_prefix(3);
                const std::size_t body = text.find_first_not_of(" \t");
                if (body != std::string_view::npos)
                    text.remove_prefix(body);
                const std::size_t last = text.find_last_not_of(" \t\r");
                text = last == std::string_view::npos ? std::string_view{} : text.substr(0, last + 1);
                constexpr std::string_view prefix = "END [";
                if (text.starts_with(prefix) && text.ends_with(']')) {
                    const std::string_view stable = text.substr(prefix.size(), text.size() - prefix.size() - 1);
                    if (!stable.empty())
                        return HeaderSynopsisEnd{std::string(stable), begin};
                }
            }
        }
        if (newline == std::string_view::npos)
            break;
        begin = newline + 1;
    }
    return std::nullopt;
}

void append_synopsis_code(beman::specgen::ir::CodeText& out, beman::specgen::ir::CodeText part) {
    if (part.text.empty())
        return;
    if (!out.text.empty() && !out.text.ends_with("\n\n")) {
        if (!out.text.ends_with('\n'))
            out.text.push_back('\n');
        out.text.push_back('\n');
    }
    const std::size_t base = out.text.size();
    out.text += part.text;
    out.spans.append_range(part.spans | std::views::transform([base](ir::Span span) {
                               span.begin += base;
                               span.end += base;
                               return span;
                           }));
}

// A declaration that ordinary classification intentionally ignores still
// belongs in a bounded header synopsis. Extract it through its semicolon,
// applying the same qualifier, exposition-use, and index sentinel machinery
// as the established class and namespace-exposition paths.
beman::specgen::ir::CodeText extract_header_declaration(clang::Decl*                                     decl,
                                                        const clang::SourceManager&                      sm,
                                                        const clang::LangOptions&                        lang_opts,
                                                        const std::set<std::string>&                     ns_drop_set,
                                                        const std::map<const clang::Decl*, std::string>& expos_set) {
    const unsigned        decl_begin = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    clang::SourceLocation end_loc    = clang::Lexer::getLocForEndOfToken(decl->getEndLoc(), 0, sm, lang_opts);
    if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(decl->getEndLoc(), sm, lang_opts);
        semi && semi->is(clang::tok::semi))
        end_loc = clang::Lexer::getLocForEndOfToken(semi->getLocation(), 0, sm, lang_opts);
    const unsigned decl_end = sm.getDecomposedLoc(end_loc).second;

    const llvm::StringRef           buffer = sm.getBufferData(sm.getMainFileID());
    std::string                     text   = buffer.substr(decl_begin, decl_end - decl_begin).str();
    std::vector<SynopsisEdit>       edits;
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;

    edits.append_range(
        namespace_qualifier_edits(decl, ns_drop_set, sm, lang_opts) |
        std::views::filter([&](const auto& range) { return range.first >= decl_begin && range.second <= decl_end; }) |
        std::views::transform([](const auto& range) { return SynopsisEdit{range.first, range.second, ""}; }));
    // substrate generic algorithm: a conditional edit fold over resolved
    // exposition uses; each accepted use can contribute two source edits.
    for (const ExposUse& use : expos_uses(decl, expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin || use.name_end > decl_end)
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }

    if (const auto* named = llvm::dyn_cast<clang::NamedDecl>(decl);
        named != nullptr && !named->getNameAsString().empty() && named->getLocation().isValid()) {
        clang::SourceRange name_range{named->getLocation(), named->getLocation()};
        if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(named))
            name_range = fn->getNameInfo().getSourceRange();
        else if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(named))
            name_range = tmpl->getTemplatedDecl()->getNameInfo().getSourceRange();
        const unsigned name_begin = sm.getDecomposedLoc(name_range.getBegin()).second;
        const unsigned name_end =
            sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(name_range.getEnd(), 0, sm, lang_opts)).second;
        if (name_begin >= decl_begin && name_end <= decl_end &&
            !std::ranges::any_of(edits, [&](const SynopsisEdit& edit) {
                return edits_overlap(edit, SynopsisEdit{name_begin, name_end, {}});
            })) {
            const std::string sentinel = span_sentinel(span_n++);
            sentinels[sentinel]        = SpanInfo{ir::SpanKind::LibraryIndex, named->getNameAsString(), ""};
            edits.push_back(SynopsisEdit{name_begin, name_end, sentinel});
        }
    }

    std::sort(
        edits.begin(), edits.end(), [](const SynopsisEdit& a, const SynopsisEdit& b) { return a.begin > b.begin; });
    unsigned applied_begin = decl_end + 1;
    // substrate generic algorithm: descending source-edit application with an
    // overlap watermark, the same operation as the established extractors.
    for (const SynopsisEdit& edit : edits) {
        if (edit.end > applied_begin)
            continue;
        text.replace(edit.begin - decl_begin, edit.end - edit.begin, edit.replacement);
        applied_begin = edit.begin;
    }
    return format_and_recover(std::move(text), sentinels);
}

beman::specgen::ir::CodeText header_comment_code(std::string text) {
    if (text.empty())
        return {};
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;
    text                                   = apply_ref_sentinels(std::move(text), span_n, sentinels);
    return recover_sentinels(std::move(text), sentinels);
}

} // namespace

InterleaveResult collect_interleaved(std::string_view header_path, const ParseOptions& options) {
    InterleaveResult result;

    ParsedHeader parsed = parse_header(header_path, options);
    if (!parsed.ast)
        return result;
    // Carried through rather than acted on here — dump-decls, this
    // function's one caller, is the debugging view of exactly what the parse
    // produced, so seeing a partial collection when the parse is partial is
    // the entire point of the command (see InterleaveResult, BuildFailure).
    result.had_parse_error = parsed.had_error;

    clang::ASTContext&    ctx       = parsed.ast->getASTContext();
    clang::SourceManager& sm        = parsed.ast->getSourceManager();
    const clang::FileID   main_file = sm.getMainFileID();

    // Main-file top-level declarations (design §3.1: "process only decls whose
    // location is in the main file"). top_level_begin()/end() is a
    // std::vector<Decl*>::iterator pair with no range object of its own;
    // std::ranges::subrange gives it one.
    std::vector<clang::Decl*> decls;
    // substrate generic algorithm: a for_each-shaped walk driving
    // collect_top_level_decl's side effect (recursively appending to
    // `decls`), not a transform building a fresh container -- this tree
    // declines ranges::for_each as a costume for a loop with a side effect
    // (see collect_inclass_items's docblock-comment note above).
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_top_level_decl(decl, sm, main_file, decls);
    result.items.append_range(decls | std::views::transform([&sm](clang::Decl* decl) {
                                  const auto [file_id, offset] = sm.getDecomposedLoc(decl->getBeginLoc());
                                  return SourceItem{SourceItem::Kind::Declaration, offset, decl_label(decl)};
                              }));

    // Main-file raw comments (design §3.1/§3.2), keyed by begin offset;
    // getCommentsInFile may return null when the file has none.
    if (const std::map<unsigned, clang::RawComment*>* comments = ctx.Comments.getCommentsInFile(main_file)) {
        result.items.append_range(*comments | std::views::transform([&sm](const auto& kv) {
            return SourceItem{SourceItem::Kind::Comment, kv.first, kv.second->getRawText(sm).str()};
        }));
    }

    // Design §3.2: "collect top-level decls and raw comments; sort by source
    // offset; interleave." Ties keep decls before comments, which cannot
    // happen in practice — a comment and a decl cannot begin at the same byte.
    std::stable_sort(result.items.begin(), result.items.end(), [](const SourceItem& a, const SourceItem& b) {
        return a.offset < b.offset;
    });

    return result;
}

std::expected<db::BuildResult, BuildFailure> build_document(std::string_view    header_path,
                                                            const ParseOptions& options) {
    namespace ir = beman::specgen::ir;

    ParsedHeader parsed = parse_header(header_path, options);
    if (!parsed.ast)
        return std::unexpected(
            BuildFailure{std::format("cannot read '{}', or Clang could not parse it", header_path)});
    // Distinct from the message above, and deliberately so — Clang did
    // build an AST here, but its own DiagnosticsEngine says the parse that
    // produced it hit an error, which buildASTFromCodeWithArgs's recovery can
    // paper over with a non-null ASTUnit (ParsedHeader's doc comment above has
    // the mechanism). Reported as a BuildFailure rather than swallowed: this
    // pipeline's extraction is lexical, so a header Clang could not fully
    // parse does not fail loudly with a missing type — it fails quietly with
    // a plausible-looking wrong one (an unlinked
    // out-of-line definition renders `gadget::`-qualified and the roster calls
    // it undocumented), and `generate`'s output is wording a reader will trust.
    if (parsed.had_error)
        return std::unexpected(BuildFailure{std::format(
            "Clang reported an error parsing '{}'; the parse is incomplete, so the wording generated from it "
            "would be unreliable",
            header_path)});

    clang::ASTContext&        ctx       = parsed.ast->getASTContext();
    clang::SourceManager&     sm        = parsed.ast->getSourceManager();
    const clang::FileID       main_file = sm.getMainFileID();
    const clang::LangOptions& lang_opts = parsed.ast->getLangOpts();

    std::vector<clang::Decl*> decls;
    // substrate generic algorithm: same iterator-pair-turned-subrange,
    // for_each-shaped side effect as collect_interleaved's identical walk
    // above.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_top_level_decl(decl, sm, main_file, decls);

    // Pre-passes: members to drop from every synopsis
    // (`\omit`/`\merge`) and exposition-only members (`\expos`) to render with an
    // `\exposid` span — both needed before the first extract_synopsis call below.
    const std::set<const clang::Decl*>              omit_set         = build_omit_set(decls, sm);
    const std::map<const clang::Decl*, std::string> expos_set        = build_expos_set(decls, sm);
    const SeeBelowMap                               seebelow_map     = build_seebelow_map(decls, sm);
    const FreestandingMap                           freestanding_map = build_freestanding_map(decls, sm);
    const std::set<std::string>                     ns_drop_set      = build_namespace_drop_set(decls);
    const SkippedRanges                             skipped          = collect_skipped_ranges(*parsed.ast);

    std::vector<RawItem> raw_items;
    raw_items.reserve(decls.size());
    raw_items.append_range(decls | std::views::transform([&sm](clang::Decl* decl) {
                               const auto [file_id, offset] = sm.getDecomposedLoc(decl->getBeginLoc());
                               return RawItem{offset, decl, {}};
                           }));
    if (const std::map<unsigned, clang::RawComment*>* comments = ctx.Comments.getCommentsInFile(main_file)) {
        // substrate generic algorithm: one RawComment expands to one or more
        // RawItems, so this is a flat-map into the existing event buffer.
        for (const auto& [offset, comment] : *comments)
            append_rsec_comment_items(raw_items, offset, comment->getRawText(sm).str());
    }
    // Same ordering contract as collect_interleaved (design §3.2).
    std::stable_sort(
        raw_items.begin(), raw_items.end(), [](const RawItem& a, const RawItem& b) { return a.offset < b.offset; });

    // The qualifiers ns_drop_set did *not* cover, collected before the
    // pipeline goes clang-free below and attached to the finished document.
    // It is document-level data, so it rides past build_tree rather than
    // through it — no DocEvent carries it and no node owns it.
    const std::vector<ir::ForeignNamespace> foreign = collect_foreign_namespaces(decls, ns_drop_set);

    // And the members named by the bodies this pipeline never renders,
    // collected here for the same reason and carried the same way — the one
    // thing design §9's leakage checker needs that no node can hold.
    const std::vector<ir::BodyUse> body_uses = collect_unextracted_uses(decls, sm);

    // A header synopsis is an ordinary .syn section whose source range
    // is closed by an exact Doxygen `/// END [stable]` line. Fold that bounded
    // range into one ordinary SynopsisDecl before the pipeline goes clang-free;
    // no header-shaped IR node or backend path is needed.
    std::vector<db::DocEvent> events;
    const auto                classify_one = [&](const RawItem& item) {
        return classify(
            item, sm, lang_opts, omit_set, expos_set, seebelow_map, freestanding_map, ns_drop_set, skipped);
    };
    const auto boundary_diagnostic = [&](const RawItem& opener, std::string message) {
        events.push_back(db::Ignored{{db::Diagnostic{
            beman::specgen::Severity::Warning, sm.getLineNumber(main_file, opener.offset), std::move(message)}}});
    };

    // substrate generic algorithm: a stateful source-order fold whose steps
    // consume either one ordinary item or a complete bounded synopsis range.
    for (std::size_t i = 0; i < raw_items.size();) {
        const RawItem&               opener = raw_items[i];
        std::optional<SectionHeader> header;
        if (opener.decl == nullptr)
            if (const auto parsed_header = parse_rsec(opener.comment_text))
                header = parsed_header->value;
        if (!header || !std::string_view(header->stable).ends_with(".syn")) {
            events.push_back(classify_one(opener));
            ++i;
            continue;
        }

        std::optional<std::size_t>       close_index;
        std::optional<HeaderSynopsisEnd> close;
        std::optional<std::string>       boundary_error;
        // substrate generic algorithm: bounded lookahead for the first
        // structural conflict or exact matching END fence.
        for (std::size_t j = i + 1; j < raw_items.size(); ++j) {
            const RawItem& candidate = raw_items[j];
            if (candidate.decl != nullptr)
                continue;
            if (const auto nested = parse_rsec(candidate.comment_text); nested) {
                boundary_error = std::format(
                    "header synopsis [{}] is not closed before section [{}]", header->stable, nested->value.stable);
                break;
            }
            const auto end = header_synopsis_end(candidate.comment_text);
            if (!end)
                continue;
            if (end->stable != header->stable) {
                boundary_error =
                    std::format("header synopsis [{}] has mismatched END [{}]", header->stable, end->stable);
                break;
            }
            close_index = j;
            close       = end;
            break;
        }

        events.push_back(classify_one(opener));
        if (!close_index) {
            boundary_diagnostic(
                opener,
                boundary_error.value_or(std::format("header synopsis [{}] has no matching END", header->stable)));
            ++i;
            continue;
        }

        db::SynopsisDecl gathered;
        gathered.offset = opener.offset + 1;
        // substrate generic algorithm: a source-order fold composing semantic
        // CodeText fragments while the outer cursor skips the consumed range.
        for (std::size_t j = i + 1; j <= *close_index; ++j) {
            RawItem item = raw_items[j];
            if (j == *close_index)
                item.comment_text.resize(close->line_begin);

            if (item.decl != nullptr) {
                const lowering::ItemDirectives directives = docblock_directives(item.decl, sm);
                db::DocEvent                   classified = classify_one(item);
                if (auto* synopsis = std::get_if<db::SynopsisDecl>(&classified);
                    synopsis != nullptr && !synopsis->synopsis.code.text.empty()) {
                    append_synopsis_code(gathered.synopsis.code, std::move(synopsis->synopsis.code));
                    gathered.diagnostics.append_range(std::move(synopsis->diagnostics));
                } else {
                    if (auto* ignored = std::get_if<db::Ignored>(&classified))
                        gathered.diagnostics.append_range(std::move(ignored->diagnostics));
                    if (!directives.omit && !directives.merge)
                        append_synopsis_code(
                            gathered.synopsis.code,
                            extract_header_declaration(item.decl, sm, lang_opts, ns_drop_set, expos_set));
                }
                continue;
            }
            if (item.comment_text.empty())
                continue;

            db::DocEvent classified = classify_one(item);
            if (auto* synopsis = std::get_if<db::SynopsisDecl>(&classified)) {
                append_synopsis_code(gathered.synopsis.code, std::move(synopsis->synopsis.code));
                gathered.diagnostics.append_range(std::move(synopsis->diagnostics));
                continue;
            }

            // Of ordinary comments, only draft synopsis group headers are
            // semantic here. Markup, Doxygen, and namespace-closing comments
            // are source scaffolding and do not enter the gathered node.
            std::string refs;
            std::size_t line_begin = 0;
            // substrate generic algorithm: scan physical lines so only exact
            // draft Ref headers survive from a possibly merged RawComment.
            while (line_begin <= item.comment_text.size()) {
                const std::size_t      newline  = item.comment_text.find('\n', line_begin);
                const std::size_t      line_end = newline == std::string::npos ? item.comment_text.size() : newline;
                const std::string_view line(item.comment_text.data() + line_begin, line_end - line_begin);
                if (line_vocabulary(llvm::StringRef{line}.ltrim(" \t")) == CommentVocabulary::Draft &&
                    parse_ref(line)) {
                    if (!refs.empty())
                        refs.push_back('\n');
                    refs.append(line);
                }
                if (newline == std::string::npos)
                    break;
                line_begin = newline + 1;
            }
            append_synopsis_code(gathered.synopsis.code, header_comment_code(std::move(refs)));
        }
        events.push_back(std::move(gathered));
        i = *close_index + 1;
    }

    // Stages 2-3: build_tree() folds the `\rSec` frame stack into a tree,
    // grouping each frame's own `\also`/empty-descr followers onto their
    // primaries (via group_items()) in push order as that frame closes —
    // see document_build.hpp's top-of-file note for why stage 3 runs there
    // rather than as a separate pass over the finished tree. Both clang-free,
    // both unit-tested with synthetic events in
    // tests/beman/specgen/document_build.test.cpp.
    db::BuildResult built             = db::build_tree(events);
    built.document.foreign_namespaces = foreign;
    built.document.unextracted_uses   = body_uses;
    return built;
}

} // namespace beman::specgen::frontend
