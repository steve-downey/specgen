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
#include <llvm/Support/Path.h>
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

// The files whose declarations form one document (issue #77): the main file,
// and the files `#include`d inside a gathered `.syn` region.  Anything else
// reached through an include is implementation, exactly as it was when a
// document was one file -- the region is the author's statement of which
// includes are the header's specification surface, which is what a header
// synopsis is.
//
// Positions are *document* offsets: the position in a virtual concatenation
// where each followed include is replaced by that file's contents.  Every rule
// that orders or compares positions -- region extents, the consumed-range
// watermark, placement keys, `\also` adjacency -- keeps working against them
// unchanged, which is why the mapping is this one and not a synthetic counter.
// Extraction is unaffected: it works inside one declaration, in its own file's
// buffer, at that file's own offsets.
class DocumentFiles {
  public:
    struct Part {
        clang::FileID file;
        unsigned      include_offset = 0; // where the main file includes it
        unsigned      size           = 0;
        unsigned      base           = 0; // its first document offset
    };

    static DocumentFiles discover(clang::ASTUnit& ast, const clang::SourceManager& sm);

    clang::FileID main() const { return main_; }

    bool contains(clang::FileID file) const {
        return file == main_ || std::ranges::any_of(parts_, [&](const Part& p) { return p.file == file; });
    }

    // The document offset of a location, or of a (file, local offset) pair.
    unsigned offset(const clang::SourceManager& sm, clang::SourceLocation loc) const {
        const auto [file, local] = sm.getDecomposedLoc(loc);
        return offset_in(file, local);
    }

    unsigned offset_in(clang::FileID file, unsigned local) const {
        if (file == main_)
            return local + inserted_before(local);
        const auto part = std::ranges::find(parts_, file, &Part::file);
        return part == parts_.end() ? local : part->base + local;
    }

    const std::vector<Part>& parts() const { return parts_; }

    // A document offset back to the file it came from and its offset there,
    // which is what a diagnostic has to name: a line number in the virtual
    // concatenation is a line in no file anyone can open.
    std::pair<clang::FileID, unsigned> locate(unsigned document_offset) const {
        // substrate generic algorithm: a linear scan over the parts, which are
        // sorted and few -- one per include inside the region.
        for (const Part& part : parts_)
            if (document_offset >= part.base && document_offset < part.base + part.size)
                return {part.file, document_offset - part.base};
        unsigned shift = 0;
        // substrate generic algorithm: the same running sum inserted_before
        // makes, stopping where this offset sits rather than at a bound
        // computed in advance.
        for (const Part& part : parts_) {
            if (part.base > document_offset)
                break;
            shift += part.size + 1;
        }
        return {main_, document_offset - shift};
    }

  private:
    // How much followed content sits before a main-file offset.
    unsigned inserted_before(unsigned main_offset) const {
        unsigned shift = 0;
        // substrate generic algorithm: a running sum over the parts that
        // precede this offset, which are a prefix -- parts_ is sorted by
        // include_offset.
        for (const Part& part : parts_) {
            if (part.include_offset >= main_offset)
                break;
            shift += part.size + 1;
        }
        return shift;
    }

    clang::FileID     main_;
    std::vector<Part> parts_;
};

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
                            const DocumentFiles&        doc,
                            std::vector<clang::Decl*>&  out) {
    if (auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(decl)) {
        // substrate generic algorithm: recursive descent over a decl tree --
        // the recursion is the algorithm, and this loop only feeds it one
        // child at a time; it is not a fold over a range that already
        // exists, and this tree declines ranges::for_each as a costume for a
        // loop whose job is a side effect (the recursive call), not building
        // a container.
        for (clang::Decl* child : ns->decls())
            collect_top_level_decl(child, sm, doc, out);
        return;
    }
    if (auto* linkage = llvm::dyn_cast<clang::LinkageSpecDecl>(decl)) {
        // substrate generic algorithm: same recursive tree descent as above.
        for (clang::Decl* child : linkage->decls())
            collect_top_level_decl(child, sm, doc, out);
        return;
    }

    // Implicit decls are compiler synthesis, not source: an implicit
    // deduction guide reports the enclosing constructor's (or the class's)
    // own location, so collecting one plants a phantom top-level decl in the
    // middle of a class body — which a gathered header synopsis then
    // re-extracts as raw source, markup comments and all (issue #22). Only
    // authored declarations are document-tree items.
    if (decl->isImplicit())
        return;

    const clang::SourceLocation loc = decl->getBeginLoc();
    if (!loc.isValid())
        return;
    const auto [file_id, offset] = sm.getDecomposedLoc(loc);
    if (!doc.contains(file_id))
        return;

    // What the parser hands back as a top-level declaration is not always one.
    // A late-instantiated member function definition arrives here through
    // HandleTopLevelDecl, reporting the location of the template it was
    // instantiated from -- so a document that follows the header holding the
    // pattern (issue #77) collected a class's private member as a namespace
    // declaration and folded its whole body into the synopsis. Two questions
    // separate an authored declaration from an instantiation of one: whose
    // scope it is written in, and whether it is a specialization of something.
    // *Lexical* scope, because an out-of-line member definition is written at
    // namespace scope and is exactly what this file's house style puts there;
    // its semantic context is the class, which is what an instantiation
    // reports too.
    if (!decl->getLexicalDeclContext()->isFileContext())
        return;
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl);
        fn != nullptr && fn->getTemplateSpecializationKind() != clang::TSK_Undeclared)
        return;
    if (llvm::isa<clang::ClassTemplateSpecializationDecl>(decl) &&
        llvm::cast<clang::ClassTemplateSpecializationDecl>(decl)->getSpecializationKind() !=
            clang::TSK_ExplicitSpecialization)
        return;
    // A variable template's instantiations arrive the same way, reporting the
    // template's own location: `whatwg_decode<C>` used at thirty-eight codecs
    // rendered thirty-eight identical declarations into one synopsis.  A
    // *partial* specialization is authored and stays -- that is how
    // `enable_borrowed_range` is written.
    if (const auto* var_spec = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl);
        var_spec != nullptr && !llvm::isa<clang::VarTemplatePartialSpecializationDecl>(var_spec) &&
        var_spec->getSpecializationKind() != clang::TSK_ExplicitSpecialization)
        return;

    out.push_back(decl);
}

// The same descent, run for the `\expos` pre-pass alone and over the
// declarations the walk above rejects: the ones outside the main file (issue
// #36). Where an exposition-only helper is *declared* is not a property of
// the wording. A concept in `detail/range_traits.hpp` named by a
// requires-clause in the header being specified must still render
// `$const-iterable$`; without this the marker on it was silently ignored, and
// the only remedies the qualifier finding offered -- rewrite the name, or move
// the entity out of `detail` -- were both edits to the library, made to suit
// the generator, and both of them change name lookup and ADL.
//
// Only the marked declaration's *uses* are reached this way. It contributes no
// node of its own, because document structure is still main-file-only (design
// §3.1) and every routing arm reads the walk above; an author who wants the
// helper's own declaration in the wording writes it in the header being
// specified, which is where the draft would put it.
//
// A system header is skipped whole, namespace and all: nothing in one carries
// specgen markup, and descending `namespace std` to establish that is
// thousands of declarations per parse.
void collect_expos_scope_decl(clang::Decl*                decl,
                              const clang::SourceManager& sm,
                              clang::FileID               main_file,
                              std::vector<clang::Decl*>&  out) {
    const clang::SourceLocation loc = decl->getBeginLoc();
    if (!loc.isValid() || sm.isInSystemHeader(loc))
        return;
    if (auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(decl)) {
        // substrate generic algorithm: the same recursive tree descent as
        // collect_top_level_decl, for the same reason.
        for (clang::Decl* child : ns->decls())
            collect_expos_scope_decl(child, sm, main_file, out);
        return;
    }
    if (auto* linkage = llvm::dyn_cast<clang::LinkageSpecDecl>(decl)) {
        // substrate generic algorithm: same recursive tree descent as above.
        for (clang::Decl* child : linkage->decls())
            collect_expos_scope_decl(child, sm, main_file, out);
        return;
    }
    if (decl->isImplicit() || sm.getDecomposedLoc(loc).first == main_file)
        return; // implicit, or already collected by collect_top_level_decl

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

// The buffer and the comment map of the file a location is in, rather than of
// the main file (issue #77).  Extraction is per declaration and every offset it
// computes is local to that declaration's own file, so a document that spans
// several files reads each one's text where it used to read the main file's.
// For a document that is one file these are the main file, as before.
llvm::StringRef file_buffer(const clang::SourceManager& sm, clang::SourceLocation loc) {
    return sm.getBufferData(sm.getFileID(loc));
}

const std::map<unsigned, clang::RawComment*>*
file_comments(const clang::ASTContext& ctx, const clang::SourceManager& sm, clang::SourceLocation loc) {
    return ctx.Comments.getCommentsInFile(sm.getFileID(loc));
}

// The out-of-line definition of a class member, or null.  `isOutOfLine` alone
// says yes for a hidden friend, which is lexically in the class and has no
// in-class declaration to inherit a group from.
const clang::FunctionDecl* as_out_of_line_member(const clang::Decl* decl) {
    const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl);
    if (fn == nullptr)
        if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
            fn = tmpl->getTemplatedDecl();
    if (fn == nullptr || !fn->isOutOfLine())
        return nullptr;
    const clang::FunctionDecl* first = fn->getFirstDecl();
    return first != fn && llvm::isa<clang::CXXRecordDecl>(first->getLexicalDeclContext()) ? fn : nullptr;
}

// The function a declaration is, looking through a function template.
const clang::FunctionDecl* function_or_template(const clang::Decl* decl) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return fn;
    if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return tmpl->getTemplatedDecl();
    return nullptr;
}

// Is this the declaration of its entity that a synopsis should show?
//
// A synopsis lists an entity once.  In a document that is one file the question
// never arose -- out-of-line definitions live after the `/// END` fence, and a
// region held declarations only -- but a followed header (issue #77) carries
// both, and a definition folded in would list a member the class synopsis
// above it already declares, in the `Class::member` spelling the draft never
// prints.  Its *wording* still travels: only the synopsis entry is dropped.
bool is_first_declaration(const clang::Decl* decl) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(decl))
        return fn->getFirstDecl() == fn;
    if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
        return tmpl->getFirstDecl() == tmpl;
    if (const auto* var = llvm::dyn_cast<clang::VarDecl>(decl))
        return var->getFirstDecl() == var;
    return true;
}

// How a diagnostic names the file a finding is in: empty for the main file,
// which the driver already names, and otherwise the path relative to the main
// file's own directory when the file is under it -- `document/errors.hpp` --
// so a finding reads the way the `#include` that pulled the file in does.
std::string diagnostic_file_name(const clang::SourceManager& sm, clang::FileID file) {
    if (file == sm.getMainFileID() || file.isInvalid())
        return {};
    const clang::OptionalFileEntryRef entry = sm.getFileEntryRefForID(file);
    const clang::OptionalFileEntryRef main  = sm.getFileEntryRefForID(sm.getMainFileID());
    if (!entry)
        return {};
    const llvm::StringRef name = entry->getName();
    if (main) {
        const llvm::StringRef dir = llvm::sys::path::parent_path(main->getName());
        if (!dir.empty() && name.starts_with(dir) && name.size() > dir.size() + 1)
            return name.substr(dir.size() + 1).str();
    }
    return llvm::sys::path::filename(name).str();
}

// The line a document offset is on, in the file it is actually in: a line
// number in the virtual concatenation is a line in no file anyone can open.
unsigned document_line(const clang::SourceManager& sm, const DocumentFiles& doc, unsigned document_offset) {
    const auto [file, local] = doc.locate(document_offset);
    return sm.getLineNumber(file, local);
}

// A skipped preprocessor range, and the file it is in: the ranges are compared
// against offsets local to one declaration's own file, and a document can span
// several (issue #77), so a range from another file must not match.
struct SkippedRange {
    clang::FileID file;
    unsigned      begin = 0;
    unsigned      end   = 0;
};

using SkippedRanges = std::vector<SkippedRange>;

SkippedRanges collect_skipped_ranges(clang::ASTUnit& ast) {
    SkippedRanges               result;
    const clang::SourceManager& sm     = ast.getSourceManager();
    clang::PreprocessingRecord* record = ast.getPreprocessor().getPreprocessingRecord();
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
        if (begin_file == end_file && end > begin)
            result.emplace_back(begin_file, begin, end);
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
    style.PointerAlignment = clang::format::FormatStyle::PAS_Left;
    // And it writes the qualifier on the left, always: `const T&`, never
    // `T const&`. Left unset, clang-format keeps whichever order the header
    // used, so the draft's spelling depended on the library's house style
    // (issue #39). A header may write either; the wording says `const T&`.
    //
    // QualifierOrder is set alongside, and has to be: clang-format derives it
    // from QualifierAlignment only while *parsing YAML*, so a style built in
    // code gets the enum and an empty order, and the fixer it gates then has
    // nothing to reorder by and silently does nothing.
    style.QualifierAlignment            = clang::format::FormatStyle::QAS_Left;
    style.QualifierOrder                = {"const", "volatile", "type"};
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

    // Every written mention of a concept, whichever spelling wrote it: the
    // `C<T>` of a requires-clause holds one of these, and so does the `C T` of
    // a constrained template parameter, where there is no
    // ConceptSpecializationExpr to visit at all -- the shorthand's constraint
    // hangs off the TemplateTypeParmDecl (issue #48). Visiting the reference
    // rather than the expression covers both from one hook, and covers each
    // exactly once.
    bool VisitConceptReference(clang::ConceptReference* ref) {
        if (ref != nullptr)
            add(ref->getNamedConcept(), ref->getConceptNameInfo().getSourceRange(), ref->getNestedNameSpecifierLoc());
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

    // An exposition-only enumeration is written as a TagTypeLoc -- the type of
    // a member, a parameter, or a variable (issue #68). Without this hook its
    // own declaration rendered under its exposid name while every use kept the
    // real one, which is the wording naming an entity the reader cannot see.
    // A record reaches this hook too and is harmless: a plain class is not an
    // exposition candidate, and a class template's uses are template-ids,
    // handled above.
    bool VisitTagTypeLoc(clang::TagTypeLoc tl) {
        add(tl.getDecl(), clang::SourceRange(tl.getNameLoc()), tl.getQualifierLoc());
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

// A use of an exposition-only entity deletes its *whole* qualifier, so a
// namespace-qualifier edit nested inside that range has nothing left to do --
// and doing it is worse than redundant. The apply loops scatter edits in
// descending offset order and skip anything inside a range already consumed
// (design 3.4), so the nested edit is taken first and its watermark then
// suppresses the deletion it sits inside. The name replacement is a separate,
// non-overlapping range and still lands, which is how `detail::` survives
// beside an exposid that was supposed to take it along.
//
// The shape that reaches this is a qualifier whose head is droppable and whose
// whole is not: `beman::transcoding::detail::` written by a specialization
// declared in another namespace, where writing it relative to the specified
// namespace is not an option. Written as `detail::` from inside, there is no
// prefix to strip, no nested edit, and the deletion has always worked -- which
// is how one entity came to render two ways in one synopsis.
//
// Only exposition-only uses are filtered. A qualifier on anything else is the
// leakage checker's business, and `std::ranges::` must still lose its `std::`.
void drop_edits_inside_expos_qualifiers(std::vector<std::pair<unsigned, unsigned>>& edits,
                                        const std::vector<ExposUse>&                uses) {
    std::erase_if(edits, [&uses](const std::pair<unsigned, unsigned>& edit) {
        // substrate generic algorithm: a containment search over the uses, not
        // an index into them -- the qualifier ranges do not nest in each other.
        return std::ranges::any_of(uses, [&edit](const ExposUse& use) {
            return use.qualifier_end > use.qualifier_begin && edit.first >= use.qualifier_begin &&
                   edit.second <= use.qualifier_end;
        });
    });
}

// Same, over a statement/expression subtree (a requires-clause condition, a
// static_assert condition, an extracted body).
std::vector<std::pair<unsigned, unsigned>>
namespace_qualifier_edits(clang::Stmt*                                     root,
                          const std::set<std::string>&                     drop,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::map<const clang::Decl*, std::string>& expos) {
    std::vector<std::pair<unsigned, unsigned>> out;
    if (root != nullptr && !drop.empty()) {
        QualifierDropper dropper(drop, sm, lang_opts, out);
        dropper.TraverseStmt(root);
        drop_edits_inside_expos_qualifiers(out, expos_uses(root, expos, sm, lang_opts));
    }
    return out;
}

std::vector<std::pair<unsigned, unsigned>>
namespace_qualifier_edits(clang::Decl*                                     root,
                          const std::set<std::string>&                     drop,
                          const clang::SourceManager&                      sm,
                          const clang::LangOptions&                        lang_opts,
                          const std::map<const clang::Decl*, std::string>& expos) {
    std::vector<std::pair<unsigned, unsigned>> out;
    if (root != nullptr && !drop.empty()) {
        QualifierDropper dropper(drop, sm, lang_opts, out);
        dropper.TraverseDecl(root);
        drop_edits_inside_expos_qualifiers(out, expos_uses(root, expos, sm, lang_opts));
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
    const std::string file = diagnostic_file_name(sm, sm.getFileID(rc->getBeginLoc()));
    return diags | std::views::transform([&](const grammar::Diagnostic& d) {
               const unsigned line = d.line > 0 ? first_line + static_cast<unsigned>(d.line) - 1 : first_line;
               return beman::specgen::document_build::Diagnostic{d.severity, line, d.message, file};
           }) |
           std::ranges::to<std::vector<beman::specgen::document_build::Diagnostic>>();
}

// The raw comment attached to `decl`, looked up through its described
// template when it has one. Clang anchors comment search for a
// RedeclarableTemplateDecl at its `template` keyword but for the templated
// decl at its *name*, and rejects any candidate comment separated from the
// anchor by one of `;{}#@` (ASTContext::getRawCommentNoCacheImpl). A
// requires-expression in a template-head constraint —
// `requires requires(T t) { ... }` — puts all three between the header and
// the name, so a lookup through the templated decl silently loses the
// docblock that the template decl still finds (issue #20). The described
// template is also the decl clang's own comment machinery documents
// (adjustDeclToTemplate).
//
// The key is an ASTContext::RawCommentLookupKey, which LLVM 23 widened from
// `const Decl*` so that macros can carry comments too; the conversion here is
// the PointerUnion's, not a coincidence. The lookup stays the *NoCache* one on
// purpose: getRawCommentForAnyRedecl, the public alternative whose signature
// happens to span both LLVM 22 and 23, walks the redeclaration chain and finds
// comments this anchor rule is written to reject.
const clang::RawComment* attached_raw_comment(const clang::Decl* decl) {
    const clang::TemplateDecl* described = decl->getDescribedTemplate();
    return decl->getASTContext().getRawCommentNoCache(described != nullptr ? described : decl);
}

// Does `decl` carry a `//!`/`/*!` docblock of its own? The two-step the
// attach path and the roster both need before trusting a raw comment:
// attached_raw_comment yields whatever comment immediately precedes
// the decl, which may well be a draft-form `\ref`/`\rSec` line that belongs to
// the synopsis rather than markup for this entity.
bool has_docblock(const clang::Decl* decl, const clang::SourceManager& sm) {
    const clang::RawComment* rc = attached_raw_comment(decl);
    return rc != nullptr && is_docblock_comment(rc->getRawText(sm));
}

// Lower just the directives (markers) from a decl's `//!` docblock, or a
// default-constructed set if it has none. Used by the omit-set and
// expos-set pre-passes to read `\omit`/`\merge`/`\expos` without building a full
// itemdescr; attach_function lowers the descr and directives together on the
// attach path. Takes any Decl so it serves data members (`\expos`) as well as
// functions.
beman::specgen::lowering::ItemDirectives docblock_directives(const clang::Decl* decl, const clang::SourceManager& sm) {
    if (const clang::RawComment* rc = attached_raw_comment(decl)) {
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
    const clang::RawComment* rc = attached_raw_comment(decl);
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

// Everything a class or class-template *definition*'s own `//!` docblock
// contributes (design §3.4, §6): the description elements the author wrote
// about the type itself, a `\verbatim-synopsis` payload replacing extraction
// (design §4.3, issue #4), and the block's grammar findings.
//
// All three used to be conditional on a verbatim marker, so an ordinary class
// docblock produced nothing and said nothing — issue #18's silent loss. It is
// the one shape issue #1's backstop cannot catch: a definition *does* produce
// wording (its synopsis), so the docblock looks accounted for while its
// contents are gone. The findings ride along here because classify()
// suppresses the comment's own RawItem for an attached block, so this is
// where they reach the driver.
struct RecordDocblock {
    beman::specgen::ir::ItemDescr                           descr;
    std::optional<std::string>                              synopsis;
    std::vector<beman::specgen::document_build::Diagnostic> diagnostics;
};

RecordDocblock record_docblock(const clang::Decl* decl, const clang::SourceManager& sm) {
    const clang::RawComment* rc = attached_raw_comment(decl);
    if (rc == nullptr)
        return {};
    const llvm::StringRef            raw   = rc->getRawText(sm);
    const std::optional<std::size_t> start = docblock_start(raw);
    if (!start)
        return {};

    const grammar::ParseResult parsed = grammar::parse_docblock(raw.substr(*start).str());

    RecordDocblock out;
    out.diagnostics = docblock_diagnostics(rc, *start, parsed.diags, sm);
    out.descr       = lowering::lower(parsed.block).descr;

    const auto reject = [&](std::string message) {
        const std::vector<grammar::Diagnostic> invalid{{beman::specgen::Severity::Error, 0, std::move(message)}};
        out.diagnostics.append_range(docblock_diagnostics(rc, *start, invalid, sm));
    };

    if (parsed.block.verbatim_itemdecl)
        reject("\\verbatim-itemdecl applies to a declaration; a class definition takes \\verbatim-synopsis");
    // An extraction marker (design §4.2) reads a *function definition's* body.
    // A class has none, so lowering's placeholder EquivalentTo is never filled
    // and its element would render as an "Equivalent to:" with an empty code
    // block under it -- design §9's blank box, one node over. Report it and
    // drop the placeholder, rather than print the box the Error is about.
    if (parsed.block.markers.effects_equiv || parsed.block.markers.returns_equiv) {
        reject("an extraction marker applies to a function definition, not to a class definition");
        std::erase_if(out.descr.elements, [](const beman::specgen::ir::DescriptionElement& element) {
            return element.equivalent && element.equivalent->code.text.empty();
        });
    }
    if (parsed.block.markers.verbatim_synopsis)
        out.synopsis = parsed.block.verbatim_synopsis.value_or("");
    return out;
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

// The last token of an alias declaration before its `;`. `getEndLoc()` is the
// end of the *TypeSourceInfo*, which leaves out a trailing cv-qualifier the
// same way a QualifiedTypeLoc leaves out a leading one, so an east-const RHS
// (`using token_type = detail::token const;`) ended at `token`: the itemdecl
// came out with the `const` cut off entirely, and the alias mask left it
// standing beside the placeholder (issue #39, the alias twin of #33). Falls
// back to what the AST said when no `;` follows, rather than running on.
clang::SourceLocation alias_last_token(const clang::TypeAliasDecl* alias,
                                       const clang::SourceManager& sm,
                                       const clang::LangOptions&   lang_opts) {
    const clang::SourceLocation given = alias->getEndLoc();
    clang::SourceLocation       last  = given;
    // substrate generic algorithm: a forward token walk to the declaration's
    // own `;`, whose distance is not known in advance; findNextToken advances
    // every step and gives out at end of file, so this terminates.
    for (std::optional<clang::Token> tok = clang::Lexer::findNextToken(last, sm, lang_opts); tok;
         tok                             = clang::Lexer::findNextToken(last, sm, lang_opts)) {
        if (tok->is(clang::tok::semi))
            return last;
        last = tok->getLocation();
    }
    return given;
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
    // ... and the last RHS token from the `;`, for the mirror-image reason.
    range.setEnd(alias_last_token(alias, sm, lang_opts));
    return range.isValid() ? std::optional{range} : std::nullopt;
}

// What bare `\seebelow` masks on a namespace-scope entity that is not a
// variable: the *definition*, not the declared type. An alias's right-hand
// side, an alias template's, or a concept's constraint-expression -- the part
// that is implementation and that the draft writes as *see below*. A variable
// masks its declared type instead (issue #24) and goes through `unspecified`.
//
// The alias half already had this on the wording-item path; the concept half
// had it nowhere, and neither had it on the `\expos` standalone-synopsis path,
// so the two markers did not compose and the leak the mask was for was
// reported against a declaration the author had already marked (issue #50).
std::optional<clang::SourceRange>
definition_mask_range(const clang::Decl* decl, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    const clang::TypeAliasDecl* alias = llvm::dyn_cast<clang::TypeAliasDecl>(decl);
    if (const auto* alias_tmpl = llvm::dyn_cast<clang::TypeAliasTemplateDecl>(decl))
        alias = alias_tmpl->getTemplatedDecl();
    if (alias != nullptr)
        return alias_rhs_source_range(alias, sm, lang_opts);
    if (const auto* concept_decl = llvm::dyn_cast<clang::ConceptDecl>(decl)) {
        const clang::Expr* constraint = concept_decl->getConstraintExpr();
        if (constraint != nullptr && constraint->getSourceRange().isValid())
            return constraint->getSourceRange();
    }
    return std::nullopt;
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
        case SeeBelowTarget::ReturnType: {
            const clang::SourceRange leading = fn->getReturnTypeSourceRange();
            if (leading.isValid())
                return leading;
            // getReturnTypeSourceRange() deliberately rejects a range that
            // sits after the declarator name — which is exactly an explicit
            // trailing return type, so `\seebelow` silently did nothing for
            // the SFINAE-friendly spellings most in need of masking
            // (issue #6). Mask the trailing type itself, read off the
            // FunctionTypeLoc: the declaration keeps its trailing shape and
            // renders `auto f(...) -> see below;`.
            if (const auto* proto = fn->getType()->getAs<clang::FunctionProtoType>();
                proto != nullptr && proto->hasTrailingReturn())
                if (const clang::TypeSourceInfo* tsi = fn->getTypeSourceInfo())
                    if (const auto ftl = tsi->getTypeLoc().IgnoreParens().getAs<clang::FunctionTypeLoc>())
                        return ftl.getReturnLoc().getSourceRange();
            return {};
        }
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

// §3.6 step 1's "length-padded if needed so line-breaking stays honest", which
// the design has always specified and the sentinels never were. A sentinel
// stands in for its display text only until recovery, but clang-format decides
// line breaks and continuation alignment while it is still there -- so a name
// whose exposition-only spelling is a different width came out aligned to the
// sentinel's width instead of its own, and a continuation under an opening `<`
// lined up with nothing (issue #67).
//
// Resizing them here rather than at each of the twenty allocation sites keeps
// the one rule in one place: a sentinel is exactly as wide as what replaces it.
// It also has to be here, because it is the only point that holds the whole
// fragment, which is what makes "this identifier appears nowhere else"
// checkable rather than assumed.
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

beman::specgen::ir::CodeText format_and_recover(std::string                            text,
                                                const std::map<std::string, SpanInfo>& sentinels,
                                                std::optional<std::string_view>        record_tag = std::nullopt) {
    auto [sized_text, sized] = resize_sentinels(std::move(text), sentinels);
    std::string formatted    = format_code(sized_text, draft_format_style());

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
    return recover_sentinels(std::move(formatted), sized);
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
        if (el.flat_table) {
            el.flat_table->caption = fix_paragraph(std::move(el.flat_table->caption));
            el.flat_table->column1 = fix_paragraph(std::move(el.flat_table->column1));
            el.flat_table->column2 = fix_paragraph(std::move(el.flat_table->column2));
            el.flat_table->rows    = std::move(el.flat_table->rows) | std::views::as_rvalue |
                                     std::views::transform([&](ir::Table1DRow row) {
                                      row.cell1 = fix_paragraph(std::move(row.cell1));
                                      row.cell2 = fix_paragraph(std::move(row.cell2));
                                      return row;
                                     }) |
                                     std::ranges::to<std::vector<ir::Table1DRow>>();
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

// Where the spliced-away tail of an in-class function definition begins: the
// `{` of the body — or, for a constructor with a written mem-initializer
// list, the `:` that introduces it (issue #21). A ctor-initializer is
// implementation, never interface: the draft writes the declaration alone,
// and leaving the list in place also leaks the private member's spelling and
// the dropped-qualifier rewrite (`base_(move(base))`) into the synopsis and
// itemdecl. Starting the splice at the `:` removes the list with the body,
// and the removed range then suppresses the qualifier/expos edits inside it
// as with any spliced body. If the `:` cannot be found where it must be —
// directly before the first written initializer, across whitespace only —
// fall back to the body brace rather than cut at a guessed offset.
unsigned spliced_tail_begin(const clang::FunctionDecl* fn, const clang::Stmt* body, const clang::SourceManager& sm) {
    const unsigned body_begin = sm.getDecomposedLoc(body->getBeginLoc()).second;
    const auto*    ctor       = llvm::dyn_cast<clang::CXXConstructorDecl>(fn);
    if (ctor == nullptr)
        return body_begin;

    clang::SourceLocation first_init;
    // substrate generic algorithm: a min-fold over the written initializers'
    // locations — CXXConstructorDecl::inits() is a pointer range, and the
    // written/implicit split plus the location projection has no std::ranges
    // name that reads better than the fold itself.
    for (const clang::CXXCtorInitializer* init : ctor->inits()) {
        if (!init->isWritten())
            continue;
        const clang::SourceLocation loc = init->getSourceRange().getBegin();
        if (first_init.isInvalid() || sm.getDecomposedLoc(loc).second < sm.getDecomposedLoc(first_init).second)
            first_init = loc;
    }
    if (first_init.isInvalid())
        return body_begin;

    const auto [init_file, init_begin] = sm.getDecomposedLoc(first_init);
    bool                  invalid      = false;
    const llvm::StringRef buffer       = sm.getBufferData(init_file, &invalid);
    if (invalid)
        return body_begin;
    const std::size_t colon = buffer.substr(0, init_begin).find_last_not_of(" \t\n\v\f\r");
    if (colon == llvm::StringRef::npos || buffer[colon] != ':')
        return body_begin;
    return static_cast<unsigned>(colon);
}

// The `;` that terminates a `= delete`/`= default` tail, found by raw-lexing
// forward from a declaration's recorded end to the first semicolon at
// bracket depth zero. The caller has already established from the AST that
// such a tail is there; this only measures how far it reaches, so everything
// crossed on the way belongs to it.
//
// Depth tracking is what makes the walk indifferent to how the tail is
// spelled: P2573's `= delete("reason")` and a macro invocation's argument
// list are both balanced parentheses to skip over, and the `;` is in the
// file's own text either way -- which is the whole reason the extent is
// lexed rather than derived from the expansion.
std::optional<clang::SourceLocation> deleted_or_defaulted_semi(clang::SourceLocation       end,
                                                               const clang::SourceManager& sm,
                                                               const clang::LangOptions&   lang_opts) {
    int depth = 0;
    // substrate generic algorithm: a token-at-a-time scan whose stepping
    // primitive, `Lexer::findNextToken`, is also its termination condition --
    // it returns nothing at end of file. There is no token range to fold
    // over, and the depth counter is state the walk carries, not a result it
    // accumulates.
    for (std::optional<clang::Token> tok = clang::Lexer::findNextToken(end, sm, lang_opts); tok.has_value();
         tok                             = clang::Lexer::findNextToken(tok->getLocation(), sm, lang_opts)) {
        if (tok->isOneOf(clang::tok::l_paren, clang::tok::l_square, clang::tok::l_brace))
            ++depth;
        else if (tok->isOneOf(clang::tok::r_paren, clang::tok::r_square, clang::tok::r_brace))
            --depth;
        else if (depth == 0 && tok->is(clang::tok::semi))
            return tok->getLocation();
    }
    return std::nullopt;
}

// The offset just past a class member declaration's terminating `;`: where a
// whole-declaration removal (`\merge`/`\omit`) has to stop, and where the
// `\freestanding` comment suffix is inserted (issues #85, #99).
//
// `getEndLoc()` does not reach it when the declaration carries a
// `= delete`/`= default` tail, for two independent reasons. Clang's parser
// (ParseCXXInlineMethods.cpp) extends a deleted or defaulted function's
// recorded end past the keyword only when the declarator Sema handed back is
// itself a FunctionDecl; for a member function *template* that declarator is
// the FunctionTemplateDecl, the cast fails silently, and the templated
// FunctionDecl's own end is left at the parameter list's closing `)` --
// never advanced at all (issue #85). And even where it is advanced, it stops
// at the keyword, so P2573's `= delete("reason")` leaves `("reason")`
// stranded behind the removal (issue #99).
//
// Reading the tail out of the source text does not repair either: it may be
// spelled through a macro -- `= BEMAN_EXPECTED_DELETE_MSG("...")`, which
// bemanproject/expected uses so a pre-C++26 build still gets a plain
// `delete` -- and raw-lexing forward then finds the macro's own identifier,
// never a keyword. So the *presence* of a tail is asked of the AST, whose
// answer Sema already resolved the macro for and which is therefore
// indifferent to spelling and to which preprocessor branch is live, and only
// its *extent* is lexed (decision construct-recognition).
//
// A member with no such tail keeps exactly the answer this returned before:
// the `;` right past the recorded end, or the end of that token when even
// that is not there (an in-class body, whose `}` no semicolon follows).
unsigned
member_declaration_end(const clang::Decl* decl, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    // A tail spelled through a macro can leave the recorded end inside the
    // expansion, which the raw lexer cannot walk. The file location is the
    // invocation in the source text, which is what the scans below read; for
    // every other declaration it is the recorded end unchanged.
    const clang::SourceLocation end = sm.getFileLoc(decl->getEndLoc());
    const clang::FunctionDecl*  fn  = member_function_or_template(decl);
    if (fn != nullptr && (fn->isDeletedAsWritten() || fn->isDefaulted())) {
        if (const std::optional<clang::SourceLocation> semi = deleted_or_defaulted_semi(end, sm, lang_opts))
            return sm.getDecomposedLoc(*semi).second + 1;
    }
    if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(end, sm, lang_opts);
        semi && semi->is(clang::tok::semi))
        return sm.getDecomposedLoc(semi->getLocation()).second + 1;
    return sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(end, 0, sm, lang_opts)).second;
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
// A class template's own head: the parameter list carrying its requires-clause,
// its parameters' type constraints, and their default arguments. All of it
// belongs to the ClassTemplateDecl, not to the templated record, so a rewrite
// traversal rooted at the record never reaches it -- an exposition-only concept
// named in a class-head constraint kept its implementation spelling while the
// same concept in a member's requires-clause was renamed, in the same rendered
// block (issue #48). Rooting at the ClassTemplateDecl instead would walk the
// record a second time and emit every member's edits twice.
const clang::TemplateParameterList* head_parameters(const clang::Decl* head_decl) {
    const auto* tmpl = llvm::dyn_cast_or_null<clang::TemplateDecl>(head_decl);
    return tmpl != nullptr ? tmpl->getTemplateParameters() : nullptr;
}

beman::specgen::ir::CodeText extract_synopsis(const clang::CXXRecordDecl*                      record,
                                              const clang::SourceManager&                      sm,
                                              const clang::LangOptions&                        lang_opts,
                                              const std::set<const clang::Decl*>&              omit_set,
                                              const std::map<const clang::Decl*, std::string>& expos_set,
                                              const SeeBelowMap&                               seebelow_map,
                                              const FreestandingMap&                           freestanding_map,
                                              const std::set<std::string>&                     ns_drop_set,
                                              const clang::Decl*                               head_decl = nullptr) {
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

// The masks a marked declaration's own spelling takes, applied to one
// extraction's edit list. Two of them, and they are the same idea told about
// different halves of a declaration: `unspecified` is the variable whose
// declared *type* is the implementation (issue #24), `see_below` the range --
// an alias's right-hand side, a concept's constraint-expression -- of an
// entity whose *definition* is (issue #50). Both are dominant edits, so a
// qualifier drop or expos-use rewrite inside the masked spelling loses to the
// mask rather than tripping the watermark.
//
// Shared, because a declaration is extracted by more than one path and the
// masks must not be one of the things that differ: a variable folded into a
// gathered header synopsis goes through extract_header_declaration rather than
// extract_freestanding_declaration, and used to come out unmasked, initializer
// and `detail::` and all, with the leak then reported against the marked
// declaration (issue #55).
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
        // writes the declaration alone. A copy-init's `=` goes with it.
        // (see_below below is the same idea for the kinds whose definition,
        // rather than whose declared type, is the implementation.)
        if (const clang::Expr* init = unspecified->getInit(); init != nullptr && init->getSourceRange().isValid()) {
            unsigned          init_begin = sm.getDecomposedLoc(init->getSourceRange().getBegin()).second;
            const unsigned    init_end   = sm.getDecomposedLoc(clang::Lexer::getLocForEndOfToken(
                                                                   init->getSourceRange().getEnd(), 0, sm, lang_opts))
                                               .second;
            const std::size_t equal      = llvm::StringRef(buffer.data(), init_begin).find_last_not_of(" \t\n\v\f\r");
            if (equal != llvm::StringRef::npos && buffer[equal] == '=')
                init_begin = static_cast<unsigned>(equal);
            if (init_begin >= decl_begin && init_end > init_begin)
                add_dominant_edit(edits, SynopsisEdit{init_begin, init_end, ""});
        }
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
// `unspecified` (issue #24) is the variable whose declared type bare
// `\seebelow` masks as the draft's *unspecified* placeholder, with the
// initializer dropped — the customization-point-object shape,
// `inline constexpr unspecified name;`. Null for every other caller.
beman::specgen::ir::CodeText
extract_freestanding_declaration(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                 bool                                             exposition,
                                 std::optional<std::string_view>                  record_tag  = std::nullopt,
                                 const clang::VarDecl*                            unspecified = nullptr,
                                 std::optional<clang::SourceRange>                see_below   = std::nullopt) {
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
//
// `unspecified` is the variable whose bare `\seebelow` composes with the
// `\expos` that brought it here (issue #38): the declared type is masked and
// the initializer dropped in the standalone synopsis, exactly as they are in an
// unmarked variable's itemdecl. Null for every other kind, and for a variable
// carrying no such marker.
beman::specgen::ir::CodeText
extract_namespace_expos_synopsis(const clang::NamedDecl*                          named,
                                 const clang::SourceManager&                      sm,
                                 const clang::LangOptions&                        lang_opts,
                                 const std::set<std::string>&                     ns_drop_set,
                                 const std::map<const clang::Decl*, std::string>& expos_set,
                                 const clang::VarDecl*                            unspecified = nullptr,
                                 std::optional<clang::SourceRange>                see_below   = std::nullopt) {
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
        const unsigned     body_begin = spliced_tail_begin(in_class, body, sm);
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
    edits.append_range(
        namespace_qualifier_edits(const_cast<clang::Decl*>(range_decl), ns_drop_set, sm, lang_opts, expos_set) |
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
    // Through the declaration's last token, not the type's: they differ when
    // the RHS ends in a cv-qualifier, and the semicolon is added back here.
    std::string text =
        clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(begin_loc, alias_last_token(alias, sm, lang_opts)), sm, lang_opts)
            .str();
    text += ';';

    std::map<std::string, SpanInfo> sentinels;
    std::vector<SynopsisEdit>       edits;
    edits.append_range(
        namespace_qualifier_edits(const_cast<clang::TypeAliasDecl*>(alias), ns_drop_set, sm, lang_opts, expos_set) |
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

// The draft's qualifier order applied to a fragment whose layout is already
// what the wording wants. Expression text -- a derived conjunct, an
// *Equivalent to:* body -- is taken from source verbatim and never reflowed,
// so a header writing `T const&` put `T const&` in a Constraints element while
// the itemdecl above it, which does go through clang-format, said `const T&`
// (issue #39). ColumnLimit 0 keeps the reflow off while the qualifier fixer
// runs: with no limit clang-format honours the input's own line breaks.
clang::format::FormatStyle qualifier_style() {
    clang::format::FormatStyle style = draft_format_style();
    style.ColumnLimit                = 0;
    return style;
}

// Format an expression fragment in a declaration context, then take the
// context back off.
//
// clang-format parses what it is handed, and a bare fragment carries no
// context to parse in. From LLVM 23 on it reads the `&` of
// `requires(const Impl& impl) { ... }` as a binary operator rather than a
// declarator and spaces it as one, so a derived Constraints element came out
// saying `const Impl & impl` where the draft says `const Impl&` -- the very
// spelling draft_format_style() sets PAS_Left for. The draft's spelling is not
// negotiable against a formatter defect
// (docs/plans/llvm-23-port.md#constraints-fragment-spelling), so restore the
// context instead: wrapped as a variable initializer the fragment parses as
// what it is. An initializer and not a requires-clause because *any*
// expression is a valid one, and this path also carries Mandates conjuncts
// lifted from static_assert conditions, whose grammar a requires-clause need
// not admit.
//
// The wrapper comes off by its own fixed length, and ColumnLimit 0 is what
// makes that safe: with no limit clang-format honours the input's line breaks,
// so the added prefix reflows nothing after it and a multi-line fragment
// survives the round trip unchanged. Under LLVM 22, which formatted the bare
// fragment correctly, the whole thing is a no-op.
constexpr std::string_view expr_context_prefix = "auto beman_specgen_expr = ";

std::string format_expr_fragment(const std::string& text) {
    std::string formatted = format_code(std::format("{}{};", expr_context_prefix, text), qualifier_style());
    // Should the wrapper not come back intact, format the fragment on its own
    // rather than guess at where it now starts and ends: format_code already
    // degrades to its input when clang-format fails, and this keeps that the
    // only failure mode.
    if (!formatted.starts_with(expr_context_prefix) || !formatted.ends_with(';'))
        return format_code(text, qualifier_style());
    formatted.pop_back();
    formatted.erase(0, expr_context_prefix.size());
    return formatted;
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
        namespace_qualifier_edits(const_cast<clang::Expr*>(e), ns_drop_set, sm, lang_opts, expos_set) |
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
    return recover_sentinels(format_expr_fragment(text), sentinels);
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

// The class-scope static_assert derivation (design §5.2): one adjacent
// general-subclause paragraph, plus the conjuncts it was folded from. The
// conjuncts are the same validator-only drift evidence a member's derived
// Mandates carries -- an authored class `\mandates` replaces the paragraph
// and inherits them, so drift detection still sees the assertions the
// authored text is standing in for.
struct ClassMandates {
    beman::specgen::ir::FreeParagraph          text;
    std::vector<beman::specgen::ir::Paragraph> conjuncts;
};

std::optional<ClassMandates> derive_class_mandates(const clang::CXXRecordDecl*                      record,
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

    std::vector<ir::Paragraph> conditions = leaves | std::views::transform([&](const clang::Expr* leaf) {
                                                return phrase_conjunct(leaf, sm, lang_opts, ns_drop_set, expos_set);
                                            }) |
                                            std::ranges::to<std::vector<ir::Paragraph>>();

    ir::Paragraph text;
    text.push_back(ir::TextInline{"A program that instantiates "});
    text.push_back(ir::CodeInline{ir::CodeText{class_instantiation_name(record), {}}});
    text.push_back(ir::TextInline{" is ill-formed unless "});
    text.append_range(beman::specgen::conjuncts::join_sentence(conditions));
    return ClassMandates{ir::FreeParagraph{std::move(text)}, std::move(conditions)};
}

// The description half of a class or class-template definition's own
// docblock (design §6, issue #18), folded into the SynopsisDecl the two
// definition arms of classify() build. `record` is the templated
// CXXRecordDecl for a template, so the derivation and the exposition-only
// spellings both come off the decl that carries the members.
//
// An authored `\mandates` replaces the derived class-scope paragraph and
// inherits its conjuncts, exactly as attach_function's attach_derivation does
// for a member (design §5.2): the derivation stays validator-only drift
// evidence rather than a second paragraph saying the same thing twice.
void attach_class_description(beman::specgen::document_build::SynopsisDecl&    out,
                              RecordDocblock&&                                 block,
                              const clang::CXXRecordDecl*                      record,
                              const clang::SourceManager&                      sm,
                              const clang::LangOptions&                        lang_opts,
                              const std::set<std::string>&                     ns_drop_set,
                              const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace ir = beman::specgen::ir;

    out.descr = std::move(block.descr);
    // Within the class's own wording, uses of its exposition-only members
    // render as \exposid -- the same rewrite a member's description gets.
    rewrite_prose_expos(out.descr, expos_spellings(record, expos_set));

    std::optional<ClassMandates> derived = derive_class_mandates(record, sm, lang_opts, ns_drop_set, expos_set);
    if (!derived)
        return;
    const auto authored = std::ranges::find_if(out.descr.elements, [](const ir::DescriptionElement& element) {
        return element.kind == ir::ElementKind::Mandates && !element.derived;
    });
    if (authored != out.descr.elements.end())
        authored->conjuncts = std::move(derived->conjuncts);
    else
        out.general = std::move(derived->text);
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
std::vector<std::pair<unsigned, unsigned>> conditional_body_edits(
    const llvm::StringRef& buffer, clang::FileID file, unsigned begin, unsigned end, const SkippedRanges& skipped) {
    std::vector<std::pair<unsigned, unsigned>> edits;
    // substrate generic algorithm: clip-and-filter into an existing edit set;
    // the optional output makes a plain transform the wrong shape.
    for (const SkippedRange& range : skipped) {
        if (range.file != file)
            continue;
        const unsigned clipped_begin = std::max(begin, range.begin);
        const unsigned clipped_end   = std::min(end, range.end);
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
                                                              clang::SourceLocation       in_file,
                                                              unsigned                    begin,
                                                              unsigned                    end) {
    std::vector<std::pair<unsigned, unsigned>> edits;
    const auto*                                comments = file_comments(ctx, sm, in_file);
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

    const llvm::StringRef                      buffer = file_buffer(sm, prologue.extraction_first->getBeginLoc());
    std::vector<std::pair<unsigned, unsigned>> deletions =
        namespace_qualifier_edits(const_cast<clang::CompoundStmt*>(body), ns_drop_set, sm, lang_opts, expos_set);
    deletions.append_range(
        conditional_body_edits(buffer, sm.getFileID(prologue.extraction_first->getBeginLoc()), begin, end, skipped));
    deletions.append_range(
        body_comment_edits(ctx, sm, lang_opts, buffer, prologue.extraction_first->getBeginLoc(), begin, end));
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
    // The file `inclass_offset` is an offset in.  A document can span several
    // files (issue #77), and placement keys are compared across all of them,
    // so the pair is what identifies a position -- item_decl_event turns it
    // into the document offset that ordering actually uses.
    clang::FileID file;
    unsigned      grouping_line = 0; // first line of the docblock carrying grouping metadata
    // \verbatim-itemdecl payload (design §4.3, issue #4): exact, span-free
    // text that *replaces* the extracted declaration — the attach path that
    // would extract one uses this instead when it is engaged, and classify()
    // suppresses the comment's own standalone node so the item appears once.
    std::optional<std::string> verbatim_itemdecl = {};
};

// What bare `\seebelow` on a namespace-scope variable (template) asks for: the
// declared type masked as the draft's *unspecified* placeholder and the
// initializer dropped — the customization-point-object shape (issue #24). The
// targeted forms have no variable meaning, so one is an Error rather than the
// silent no-op the bare form used to be.
//
// Both variable paths read the marker through here. On an ordinary wording item
// it is the itemdecl that carries the mask; marked `\expos` as well, the two
// compose (issue #38) and the standalone synopsis carries it, an
// exposition-only object of unspecified type being how the draft spells a
// helper of this kind. Read on only one of the two paths, the other one
// silently ignored the marker.
struct VariableMask {
    const clang::VarDecl*                                     unspecified = nullptr;
    std::optional<beman::specgen::document_build::Diagnostic> diagnostic  = {};
};

VariableMask variable_seebelow_mask(const clang::Decl*                              decl,
                                    const beman::specgen::lowering::ItemDirectives& directives,
                                    unsigned                                        grouping_line) {
    // An enumeration is the one documented namespace entity with nothing for
    // `\seebelow` to write: a variable masks its type, an alias and a concept
    // their definitions, and the draft never spells an enum-base or an
    // enumerator list *see below*. Reported rather than ignored, because the
    // marker's whole job is to make the wording say less than the code, and a
    // marker that silently does nothing leaves a *Remarks* claiming something
    // the synopsis beside it contradicts.
    if (llvm::isa<clang::EnumDecl>(decl) && directives.seebelow && grouping_line != 0)
        return {nullptr,
                beman::specgen::document_build::Diagnostic{
                    beman::specgen::Severity::Error, grouping_line, "an enumeration accepts no \\seebelow"}};
    const clang::VarDecl* variable = llvm::dyn_cast<clang::VarDecl>(decl);
    if (const auto* var_tmpl = llvm::dyn_cast<clang::VarTemplateDecl>(decl))
        variable = var_tmpl->getTemplatedDecl();
    if (variable == nullptr || !directives.seebelow || grouping_line == 0)
        return {};
    if (directives.seebelow_target)
        return {nullptr,
                beman::specgen::document_build::Diagnostic{
                    beman::specgen::Severity::Error, grouping_line, "a variable accepts only bare \\seebelow"}};
    return {variable, std::nullopt};
}

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

    const clang::RawComment* rc = attached_raw_comment(decl);
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
    attached.verbatim_itemdecl         = pr.block.verbatim_itemdecl;
    if (attached.directives.verbatim_synopsis && attached.grouping_line > 0)
        // The synopsis form belongs on a class definition; on anything an
        // AttachedItem renders it would vanish silently, and silence is the
        // failure mode issue #1 taught this file to refuse.
        attached.diagnostics.push_back({beman::specgen::Severity::Error,
                                        attached.grouping_line,
                                        "\\verbatim-synopsis applies to a class or class template definition"});
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
    attached.is_function_def               = true;
    const clang::SourceLocation anchor_loc = friend_begin.isValid() ? friend_begin : decl_form->getBeginLoc();
    attached.inclass_offset                = sm.getDecomposedLoc(anchor_loc).second;
    attached.file                          = sm.getFileID(anchor_loc);

    // Descr: the definition's own `//!` docblock, if it has one.
    // attached_raw_comment yields the immediately preceding comment;
    // is_docblock_comment guards against picking up a draft-form `\ref`/
    // `\rSec` comment that happens to sit just above the definition. Read
    // ahead of the itemdecl (below) because `\constraints-in-decl`, carried
    // on `attached.directives`, decides whether the requires-clause is
    // stripped from it.
    if (const clang::RawComment* rc = attached_raw_comment(def)) {
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
            attached.item.descr        = std::move(lowered.descr);
            attached.directives        = std::move(lowered.directives);
            attached.verbatim_itemdecl = pr.block.verbatim_itemdecl;
            if (attached.directives.impdef) {
                const std::vector<grammar::Diagnostic> invalid{
                    {beman::specgen::Severity::Error, 0, "\\impdef applies only to type aliases"}};
                attached.diagnostics.append_range(docblock_diagnostics(rc, *start, invalid, sm));
            }
            if (attached.directives.verbatim_synopsis) {
                const std::vector<grammar::Diagnostic> invalid{
                    {beman::specgen::Severity::Error,
                     0,
                     "\\verbatim-synopsis applies to a class or class template definition"}};
                attached.diagnostics.append_range(docblock_diagnostics(rc, *start, invalid, sm));
            }
        }
    }

    // Itemdecl: the in-class declaration's own text (design §3.3), one
    // signature per SpecItem here — `\also` grouping appends further
    // signatures onto a group's primary in document_build::group_items, not
    // here. A `\verbatim-itemdecl` payload replaces the extraction whole
    // (design §4.3, issue #4): exact, span-free authored text, so none of the
    // seebelow/requires-stripping machinery applies to it. Otherwise, default
    // (no `\constraints-in-decl`): the requires-clause is stripped, since it
    // is derived into a Constraints element below instead (design §5.1).
    const bool strip_requires = !attached.directives.constraints_in_decl;
    if (attached.verbatim_itemdecl) {
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    } else {
        attached.item.decl.signatures.push_back(extract_itemdecl(decl_form,
                                                                 sm,
                                                                 lang_opts,
                                                                 strip_requires,
                                                                 ns_drop_set,
                                                                 expos_set,
                                                                 friend_begin,
                                                                 seebelow_target(attached.directives)));
    }

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
    attached.file           = sm.getFileID(anchor->getBeginLoc());

    attach_docblock(attached, anchor, sm);
    if (attached.directives.seebelow_target && attached.grouping_line > 0)
        // Positioned at the docblock's first line, the same place
        // docblock_diagnostics puts a line-0 grammar finding.
        attached.diagnostics.push_back(
            {beman::specgen::Severity::Error, attached.grouping_line, "a type alias accepts only bare \\seebelow"});

    if (attached.verbatim_itemdecl)
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    else
        attached.item.decl.signatures.push_back(extract_alias_itemdecl(
            alias, sm, lang_opts, ns_drop_set, expos_set, alias_mask(attached.directives), head));
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
    attached.file           = sm.getFileID(decl->getBeginLoc());
    attach_docblock(attached, decl, sm);

    const llvm::StringRef tag = record->getKindName();
    if (attached.verbatim_itemdecl)
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    else
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
    attached.file           = sm.getFileID(decl->getBeginLoc());
    attach_docblock(attached, decl, sm);

    const VariableMask mask = variable_seebelow_mask(decl, attached.directives, attached.grouping_line);
    if (mask.diagnostic)
        attached.diagnostics.push_back(*mask.diagnostic);
    // A concept reaches this path too, and its definition masks the same way
    // an alias's RHS does on attach_alias's (issue #50); the marker used to be
    // accepted here with no effect and no diagnostic.
    std::optional<clang::SourceRange> see_below;
    if (attached.directives.seebelow && !attached.directives.seebelow_target)
        see_below = definition_mask_range(decl, sm, lang_opts);

    if (attached.verbatim_itemdecl)
        attached.item.decl.signatures.push_back(ir::CodeText{*attached.verbatim_itemdecl, {}});
    else
        attached.item.decl.signatures.push_back(extract_freestanding_declaration(decl,
                                                                                 sm,
                                                                                 lang_opts,
                                                                                 ns_drop_set,
                                                                                 expos_set,
                                                                                 /*exposition=*/false,
                                                                                 std::nullopt,
                                                                                 mask.unspecified,
                                                                                 see_below));
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

// A specialization and the primary it specializes, or {decl, nullptr} when
// `decl` specializes nothing. Partial and explicit alike: the partial kinds
// derive from these, so one cast each covers both (issue #49).
std::pair<const clang::Decl*, const clang::Decl*> specialized_primary(const clang::Decl* decl) {
    if (const auto* record = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl))
        return {decl, record->getSpecializedTemplate()};
    if (const auto* variable = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
        return {decl, variable->getSpecializedTemplate()};
    return {decl, nullptr};
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
    attached.file = sm.getFileID(decl->getBeginLoc());
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
// The `\ref` group headers inside a class body, by offset -- the same class
// span and comment source extract_synopsis walks.  Two callers ask it the same
// question: collect_inclass_items, for a member declared and defined in the
// class, and the gathered-region fold, for the out-of-line definition of one,
// which carries the wording in the house style this tool was written for and
// belongs to its class's group like every other member (issue #77).
class ClassRefGroups {
  public:
    ClassRefGroups(const clang::CXXRecordDecl* record,
                   const clang::SourceManager& sm,
                   const clang::LangOptions&   lang_opts) {
        const unsigned              class_begin = sm.getDecomposedLoc(record->getBeginLoc()).second;
        const clang::SourceLocation end_of_brace =
            clang::Lexer::getLocForEndOfToken(record->getEndLoc(), 0, sm, lang_opts);
        const unsigned class_end = sm.getDecomposedLoc(end_of_brace).second;

        // ascending by offset -- getCommentsInFile is offset-keyed, and
        // neither the filter nor the transform below reorders.
        if (const std::map<unsigned, clang::RawComment*>* comments =
                file_comments(record->getASTContext(), sm, record->getBeginLoc())) {
            groups_ = *comments | std::views::filter([&](const auto& kv) {
                return kv.first >= class_begin && kv.first < class_end &&
                       parse_ref(kv.second->getRawText(sm)).has_value();
            }) | std::views::transform([&](const auto& kv) {
                return std::pair<unsigned, std::string>{kv.first, *parse_ref(kv.second->getRawText(sm))};
            }) | std::ranges::to<std::vector<std::pair<unsigned, std::string>>>();
        }
    }

    // Stable name of the nearest `\ref` group at or before `offset`, else "".
    // groups_ is ascending, so the answer is the element just before the
    // partition point of "ref_offset <= offset" -- a binary search, not a
    // linear scan with a break.
    std::string section_for(unsigned offset) const {
        const auto it = std::ranges::partition_point(groups_, [&](const auto& rg) { return rg.first <= offset; });
        return it == groups_.begin() ? std::string{} : std::prev(it)->second;
    }

  private:
    std::vector<std::pair<unsigned, std::string>> groups_;
};

void collect_inclass_items(const clang::CXXRecordDecl*                               record,
                           const clang::SourceManager&                               sm,
                           const clang::LangOptions&                                 lang_opts,
                           const std::set<std::string>&                              ns_drop_set,
                           const std::map<const clang::Decl*, std::string>&          expos_set,
                           const SkippedRanges&                                      skipped,
                           std::vector<beman::specgen::document_build::PendingItem>& pending,
                           std::vector<beman::specgen::document_build::Diagnostic>&  diagnostics) {
    const ClassRefGroups ref_groups(record, sm, lang_opts);
    const auto           section_for = [&ref_groups](unsigned offset) { return ref_groups.section_for(offset); };

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
            // An `\expos` alias is exposition, not a routed wording item:
            // it renders in the synopsis under its exposid name, and its
            // roster entry is Expos, the same treatment expos data gets.
            if (expos_set.contains(alias->getCanonicalDecl()))
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
        // An unmarked alias is synopsis-only (design §6) — except a *private*
        // one, whose synopsis line is dropped: that one enters the roster as
        // Private, so the leakage checker can see a surviving declaration
        // naming it (`using type = raw;` naming the elided `raw`, issue #7) —
        // the same visibility question unmarked private data already answers.
        if (alias != nullptr && !has_docblock(alias, sm) && !real_member.effectively_private)
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
        // The same spelling the node's own `name` takes, and from the same
        // call: a gathered header synopsis holds several classes' entries and
        // names none of them, so this is what tells them apart (issue #45).
        entry.parent = record->getNameAsString();
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
        } else if (fn == nullptr && alias == nullptr) {
            // A *public* data member (issue #82).  Its type and its name are in
            // the synopsis and its class's own description says what it means,
            // which is how the draft specifies `from_chars_result`,
            // `ranges::in_out_result` and every other such struct: a data
            // member's specification is its declaration.  There is no itemdescr
            // to ask for -- a docblock on one produces no wording -- so
            // demanding a description asks for something that cannot exist.
            entry.disposition = ir::Disposition::Declared;
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

// A member's exposition-only name candidate: a data member, a type alias, or
// a method, including the underlying method of a FunctionTemplateDecl.
// Hidden friends
// remain namespace functions rather than class exposition members.
const clang::NamedDecl* as_field_or_method(const clang::Decl* member) {
    if (const auto* fd = llvm::dyn_cast<clang::FieldDecl>(member))
        return fd;
    if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(member))
        return alias;
    if (const auto* md = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(member_function_or_template(member)))
        return md;
    // A nested class (issue #80).  A view's iterator is one, private and named
    // by every `begin`, `end` and `operator++` signature the class publishes,
    // so it is exactly the shape `\expos` is for: rendered under an exposition
    // name rather than dropped as a private member the wording goes on naming.
    if (const auto* nested = llvm::dyn_cast<clang::CXXRecordDecl>(member);
        nested != nullptr && nested->getIdentifier() != nullptr)
        return nested;
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(member))
        return tmpl;
    return nullptr;
}

// Namespace-owned entities that can be rendered as free-standing exposition
// synopses. isFileContext includes the global namespace and named namespaces,
// while excluding an out-of-line definition of a static data member.
// Class templates belong here too (issue #23): the draft's exposition-only
// helpers are spelled as class templates as well as alias templates, and
// their template-id uses resolve through the same TemplateName hook the
// alias-template uses take.
bool is_namespace_expos_candidate(const clang::Decl* decl) {
    if (llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
        llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::ClassTemplateDecl>(decl))
        return true;
    // An enumeration belongs here for the same reason it is a wording entity
    // at all (issue #68): its definition is its interface, so an
    // exposition-only one renders as a standalone synopsis under its exposid
    // name. Without this it took the *wording* path with an empty description,
    // which made it an `\also` follower and folded its declaration into
    // whatever item preceded it.
    if (const auto* enumeration = llvm::dyn_cast<clang::EnumDecl>(decl))
        return enumeration->getDeclContext()->isFileContext();
    // A specialization of one, which is the same entity as its primary and so
    // shares its disposition (issue #49). The variable-template kinds reach
    // the VarDecl test below on their own; a class-template specialization is
    // a CXXRecordDecl and would otherwise be routed as an ordinary class.
    if (llvm::isa<clang::ClassTemplateSpecializationDecl>(decl))
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
//
// An enumeration is one of them (issue #68). It is a type whose definition is
// its interface, so the declaration is the itemdecl and the docblock's
// description follows — the draft's shape for [fs.enum.file.type], whose
// wording is a two-column table of enumerator meanings and which `\lib2dtab2`
// already renders. It was the last kind a library could document and get
// nothing for: an error said so rather than dropping it silently, but an
// error is not a way to specify an enum, and there was no other.
const clang::NamedDecl* as_namespace_entity(const clang::Decl* decl) {
    const bool entity_kind = llvm::isa<clang::ConceptDecl>(decl) || llvm::isa<clang::VarTemplateDecl>(decl) ||
                             llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::TypeAliasDecl>(decl) ||
                             llvm::isa<clang::VarDecl>(decl) || llvm::isa<clang::EnumDecl>(decl);
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
    using ExposEntry = std::pair<const clang::Decl*, std::string>;

    const auto is_marked  = [&sm](const clang::NamedDecl* named) { return docblock_directives(named, sm).expos; };
    const auto expos_name = [&sm](const clang::NamedDecl* named) {
        const beman::specgen::lowering::ItemDirectives dirs = docblock_directives(named, sm);
        return dirs.expos_name.value_or(named->getIdentifier() != nullptr
                                            ? beman::specgen::lowering::exposid_name(named->getName().str())
                                            : std::string{});
    };
    // Anonymous records are transparent here: their source members are the
    // candidates, while Clang's implicit injected projections are not.
    // The marked members of a record, and of the records nested in it: a
    // nested class's own state is exposition for the same reason its class's
    // is, and an extracted body that names it has to say the exposition name
    // (issue #80).
    const auto expos_members_of = [&](this const auto&            self,
                                      const clang::CXXRecordDecl* record) -> std::vector<ExposEntry> {
        std::vector<ExposEntry> out;
        // substrate generic algorithm: a filter-map over one record's members
        // that also descends -- the recursion is why this is a loop and not a
        // pipeline built in place.
        for (const RealRecordMember& member : real_record_members(record)) {
            if (const clang::NamedDecl* named = as_field_or_method(member.decl); named != nullptr && is_marked(named))
                out.emplace_back(named->getCanonicalDecl(), expos_name(named));
            if (const clang::CXXRecordDecl* nested = as_record_decl(member.decl);
                nested != nullptr && nested->isThisDeclarationADefinition())
                out.append_range(self(nested));
        }
        return out;
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

    // A second pass, because a specialization takes its name from the primary
    // and the primary need not have been seen first. It is the same entity, so
    // it cannot be exposition-only under one name and not under another
    // (issue #49) -- and its own markers do not enter into it: `\expos` on a
    // specialization was accepted and ignored, `\also` made it a wording item,
    // and neither kept the raw implementation spelling out of the wording.
    expos.insert_range(decls | std::views::transform(specialized_primary) |
                       std::views::filter([&expos](const auto& pair) {
                           return pair.second != nullptr && expos.contains(pair.second->getCanonicalDecl());
                       }) |
                       std::views::transform([&expos](const auto& pair) {
                           return std::pair<const clang::Decl*, std::string>{
                               pair.first->getCanonicalDecl(), expos.at(pair.second->getCanonicalDecl())};
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

// `ForeignQualifierCollector`'s complement for a name with no qualifier at
// all (issue #84): a helper sharing the header's own namespace, so nothing
// written at the use site ever names a foreign namespace, needs a check keyed
// on where the *referenced declaration* lives rather than on what the use
// site wrote. Same AST-node coverage `ExposUseFinder` reads a name from
// (`DeclRefExpr`, a concept reference, an unresolved lookup's candidates, and
// the three exposition-only type-use shapes) -- reused here for the opposite
// question: not "is this decl `\expos`-marked", but "is this decl invisible
// to the reader for a reason `\expos` was never asked to fix".
//
// Two guards keep this one-directional the same way `ForeignQualifierCollector`
// is:
//
//   - a decl already in `expos_` is skipped: it renders under its
//     `\exposid` sentinel, which is design §9's own escape hatch, and the
//     fix for a name this check *would* otherwise flag is exactly to mark it
//     `\expos` where it is actually declared -- `\expos` already reaches an
//     included, non-system header (issue #36), so no separate "trust me"
//     marker is needed;
//   - a decl in a system header is skipped: nothing under one carries specgen
//     markup, and a system header is the standard's own vocabulary, the same
//     boundary `collect_expos_scope_decl` already draws.
//
// A decl `DocumentFiles::contains` (this run's main file, or one of its
// gathered `.syn` follows, issue #77) is not "foreign" by construction: it is
// either documented through the ordinary path or reported by some other §9
// check (undocumented, `\omit`ted, private) -- this check is only about a
// declaration this run never had a chance to say anything about at all.
class ForeignDeclCollector : public clang::RecursiveASTVisitor<ForeignDeclCollector> {
  public:
    ForeignDeclCollector(const std::map<const clang::Decl*, std::string>& expos,
                         const clang::SourceManager&                      sm,
                         const DocumentFiles&                             doc,
                         std::map<std::string, std::string>&              out)
        : expos_(expos), sm_(sm), doc_(doc), out_(out) {}

    bool VisitDeclRefExpr(clang::DeclRefExpr* expr) {
        if (expr != nullptr)
            record(expr->getDecl());
        return true;
    }

    bool VisitConceptReference(clang::ConceptReference* ref) {
        if (ref != nullptr)
            record(ref->getNamedConcept());
        return true;
    }

    bool VisitUnresolvedLookupExpr(clang::UnresolvedLookupExpr* expr) {
        if (expr != nullptr)
            for (clang::NamedDecl* candidate : expr->decls()) // substrate generic algorithm
                record(candidate);
        return true;
    }

    bool VisitTypedefTypeLoc(clang::TypedefTypeLoc tl) {
        record(tl.getDecl());
        return true;
    }

    bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc tl) {
        record(tl.getTypePtr()->getTemplateName().getAsTemplateDecl());
        return true;
    }

    bool VisitTagTypeLoc(clang::TagTypeLoc tl) {
        record(tl.getDecl());
        return true;
    }

  private:
    void record(const clang::NamedDecl* decl) {
        if (decl == nullptr)
            return;
        const clang::Decl* canonical = decl->getCanonicalDecl();
        if (const auto* specialization = llvm::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
            canonical = specialization->getSpecializedTemplate()->getCanonicalDecl();
        if (expos_.contains(canonical))
            return;
        const clang::SourceLocation loc = decl->getLocation();
        if (!loc.isValid() || sm_.isInSystemHeader(loc) || doc_.contains(sm_.getDecomposedLoc(loc).first))
            return;
        const std::string name = decl->getNameAsString();
        // Bare filename, not `diagnostic_file_name`'s path-relative-to-the-
        // main-file convention: that convention assumes the file it names
        // moves in lockstep with the main file (true for a `.syn` follow,
        // which is fixed relative to it by the very `#include` that pulled it
        // in), and this decl's declaring header is under no such constraint
        // -- reached by an arbitrary `-I` or quoted-relative search, so a
        // main file relocated on its own (as the east-const golden's flipped
        // copy is) would report a different relative path for the identical
        // fact.
        if (!name.empty())
            out_.emplace(name, llvm::sys::path::filename(sm_.getFilename(loc)).str());
    }

    const std::map<const clang::Decl*, std::string>& expos_;
    const clang::SourceManager&                      sm_;
    const DocumentFiles&                             doc_;
    std::map<std::string, std::string>&              out_;
};

// Every name the document itself declares, at any depth (decision
// shared-spelling-foreign-name). The check `ForeignDeclCollector` feeds
// *resolves* a reference but *reports* it by text, and a spelling can name
// two entities: a generated `detail::tables::windows_1252` and the
// `codec::windows_1252` enumerator it is the table for are the same word and
// different declarations, deliberately, because both are named after the
// encoding. Text-matching the foreign one then fires at every occurrence of
// the documented one -- starting with the enumeration's own declaration
// (issue #93), where the check reports an enumerator as undocumented at the
// point where the enumeration that documents it is being rendered.
//
// So this is deliberately wider than the roster's `documented` set the
// validator already consults, which is what let the report through: a roster
// records the enumeration, never its enumerators, so the run genuinely did
// not know it declared that spelling. It is wider at the other end too -- a
// private or `\omit`ted member's name suppresses the foreign report as well.
// That is the direction to be wrong in. A name this run declares is this
// run's to explain, and whether it explains it well is a question every
// other design §9 check is *already* asking about that same declaration, by
// disposition rather than by text; a foreign entity sharing its spelling is
// evidence of nothing, and reporting it costs the author an `\expos` that
// asserts something false about a header the document never reaches.
//
// Locations are checked, not decl contexts: a declaration written in the
// document is the document's whatever its scope -- a class member, an
// enumerator, a template parameter, a local of a body that never becomes
// wording.
class DocumentNameCollector : public clang::RecursiveASTVisitor<DocumentNameCollector> {
  public:
    DocumentNameCollector(const clang::SourceManager& sm, const DocumentFiles& doc, std::set<std::string>& out)
        : sm_(sm), doc_(doc), out_(out) {}

    bool VisitNamedDecl(clang::NamedDecl* decl) {
        // Implicit declarations are compiler synthesis, not source, the same
        // exclusion collect_top_level_decl draws: an implicit member reports
        // its class's own location, so it would contribute a name the author
        // never wrote at a place the author never wrote it.
        if (decl == nullptr || decl->isImplicit())
            return true;
        const clang::SourceLocation loc = decl->getLocation();
        if (!loc.isValid() || !doc_.contains(sm_.getDecomposedLoc(loc).first))
            return true;
        if (const std::string name = decl->getNameAsString(); !name.empty())
            out_.insert(name);
        return true;
    }

  private:
    const clang::SourceManager& sm_;
    const DocumentFiles&        doc_;
    std::set<std::string>&      out_;
};

std::vector<beman::specgen::ir::ForeignDeclaration>
collect_foreign_declarations(const std::vector<clang::Decl*>&                 decls,
                             const std::map<const clang::Decl*, std::string>& expos,
                             const clang::SourceManager&                      sm,
                             const DocumentFiles&                             doc) {
    std::map<std::string, std::string> found;
    ForeignDeclCollector               collector(expos, sm, doc, found);
    for (clang::Decl* decl : decls) // substrate generic algorithm
        collector.TraverseDecl(decl);

    // The same walk over the same declarations, asking the other question:
    // which spellings are the document's own. Two passes rather than one
    // visitor answering both, because the two sets are keyed on opposite
    // ends of a reference -- where a *referenced* declaration lives, and
    // where a *written* declaration lives -- and only their names ever meet.
    std::set<std::string> declared;
    DocumentNameCollector names(sm, doc, declared);
    for (clang::Decl* decl : decls) // substrate generic algorithm
        names.TraverseDecl(decl);

    return found | std::views::filter([&declared](const auto& kv) { return !declared.contains(kv.first); }) |
           std::views::transform(
               [](const auto& kv) { return beman::specgen::ir::ForeignDeclaration{kv.first, kv.second}; }) |
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
    const clang::RawComment* rc = attached_raw_comment(fn);
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
beman::specgen::document_build::DocEvent item_decl_event(AttachedItem&& attached, const DocumentFiles& doc) {
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

    return db::ItemDecl{doc.offset_in(attached.file, attached.inclass_offset),
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
                            const DocumentFiles&                             doc,
                            const clang::LangOptions&                        lang_opts,
                            const std::set<std::string>&                     ns_drop_set,
                            const std::map<const clang::Decl*, std::string>& expos_set) {
    namespace db = beman::specgen::document_build;

    if (record == nullptr || record->hasDefinition())
        return db::Ignored{};
    if (!has_docblock(decl, sm))
        return db::Ignored{};
    return item_decl_event(attach_record_declaration(decl, record, sm, lang_opts, ns_drop_set, expos_set), doc);
}

// The main-file byte ranges of raw comments some processed declaration
// consumed as its own docblock (top-level decls and class members alike).
// classify() consults this for a comment carrying a verbatim marker: attached,
// the block belongs to its declaration's event, and the comment's own RawItem
// must not become a second node (issue #4).
using AttachedCommentRanges = std::vector<std::pair<unsigned, unsigned>>;

bool is_attached_comment(unsigned offset, const AttachedCommentRanges& ranges) {
    return std::ranges::any_of(
        ranges, [offset](const auto& range) { return offset >= range.first && offset <= range.second; });
}

db::DocEvent classify(const RawItem&                                   ev,
                      const clang::SourceManager&                      sm,
                      const DocumentFiles&                             doc,
                      const clang::LangOptions&                        lang_opts,
                      const std::set<const clang::Decl*>&              omit_set,
                      const std::map<const clang::Decl*, std::string>& expos_set,
                      const SeeBelowMap&                               seebelow_map,
                      const FreestandingMap&                           freestanding_map,
                      const std::set<std::string>&                     ns_drop_set,
                      const SkippedRanges&                             skipped,
                      const AttachedCommentRanges&                     attached_comments) {
    namespace ir = beman::specgen::ir;

    if (ev.decl == nullptr) {
        if (ev.comment_text.find("\\verbatim-synopsis") != std::string::npos ||
            ev.comment_text.find("\\verbatim-itemdecl") != std::string::npos) {
            const std::optional<std::size_t> start = docblock_start(ev.comment_text);
            if (start) {
                const std::string_view markup = std::string_view(ev.comment_text).substr(*start);
                grammar::ParseResult   parsed = grammar::parse_docblock(markup);
                // Attached to a declaration — and not `\omit`/`\merge`d away
                // there — the block is that declaration's own docblock: the
                // declaration's event substitutes the authored text and
                // reports the block's findings, and a second, standalone
                // node here was the duplication (issue #4). An
                // `\omit`/`\merge`d declaration contributes no node of its
                // own, so for that pairing the standalone node still speaks,
                // exactly as it does for a detached block whose entity is
                // never declared (spec_verbatim_record_merge.hpp).
                if ((parsed.block.verbatim_itemdecl || parsed.block.markers.verbatim_synopsis) &&
                    is_attached_comment(ev.offset, attached_comments) && !parsed.block.markers.omit &&
                    !parsed.block.markers.merge)
                    return db::Ignored{};
                const unsigned first_line = document_line(sm, doc, ev.offset) +
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
                                                   document_line(sm, doc, ev.offset),
                                                   std::format("malformed \\rSec marker: {} (comment offset {})",
                                                               failure.message,
                                                               failure.where.offset)}}};
            }
            if (const auto candidate = unrecognized_section_header(ev.comment_text)) {
                return db::Ignored{
                    {db::Diagnostic{beman::specgen::Severity::Warning,
                                    document_line(sm, doc, ev.offset) + candidate->line_offset,
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
        // The marker that brought it here is not the only one its docblock may
        // carry: bare `\seebelow` on a variable composes with `\expos` (issue
        // #38), and this arm used to read neither that nor the findings the
        // docblock's own parse produced.
        AttachedItem marked;
        attach_docblock(marked, ev.decl, sm);
        const VariableMask mask = variable_seebelow_mask(ev.decl, marked.directives, marked.grouping_line);
        if (mask.diagnostic)
            marked.diagnostics.push_back(*mask.diagnostic);
        // The kinds whose *definition* is the implementation mask it the same
        // way (issue #50): an alias's RHS, an alias template's, a concept's
        // constraint-expression. Bare `\seebelow` only -- the targeted forms
        // have no meaning here either, and variable_seebelow_mask has already
        // said so for the kinds it speaks for.
        std::optional<clang::SourceRange> see_below;
        if (marked.directives.seebelow && !marked.directives.seebelow_target)
            see_below = definition_mask_range(ev.decl, sm, lang_opts);

        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = namespace_expos->getNameAsString();
        out.synopsis.code = extract_namespace_expos_synopsis(
            namespace_expos, sm, lang_opts, ns_drop_set, expos_set, mask.unspecified, see_below);
        out.diagnostics = std::move(marked.diagnostics);
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
                attach_alias(alias_tmpl->getTemplatedDecl(), sm, lang_opts, ns_drop_set, expos_set, alias_tmpl), doc);
        if (const auto* alias = llvm::dyn_cast<clang::TypeAliasDecl>(entity))
            return item_decl_event(attach_alias(alias, sm, lang_opts, ns_drop_set, expos_set), doc);
        return item_decl_event(attach_namespace_entity(entity, sm, lang_opts, ns_drop_set, expos_set), doc);
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
            return classify_record_declaration(record, record, sm, doc, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(record, sm))
            return db::Ignored{std::move(*diagnostics)};
        // An attached \verbatim-synopsis replaces the extracted synopsis text
        // (design §4.3, issue #4) — authored, span-free — while members,
        // roster, and derived class wording are collected exactly as for an
        // extracted one. The rest of the same docblock is the class's own
        // description, attached below.
        RecordDocblock   block = record_docblock(record, sm);
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = record->getNameAsString();
        out.diagnostics   = std::move(block.diagnostics);
        out.synopsis.code =
            block.synopsis
                ? ir::CodeText{std::move(*block.synopsis), {}}
                : extract_synopsis(
                      record, sm, lang_opts, omit_set, expos_set, seebelow_map, freestanding_map, ns_drop_set);
        collect_inclass_items(record, sm, lang_opts, ns_drop_set, expos_set, skipped, out.pending, out.diagnostics);
        // After collection: the roster reads the routed sections
        // collect_inclass_items just decided.
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(record, sm, expos_set, out.pending);
        group_adjacent_aliases(out.pending);
        attach_class_description(out, std::move(block), record, sm, lang_opts, ns_drop_set, expos_set);
        return out;
    }
    if (const auto* tmpl = llvm::dyn_cast<clang::ClassTemplateDecl>(ev.decl)) {
        const clang::CXXRecordDecl* templated = tmpl->getTemplatedDecl();
        if (templated == nullptr || !templated->isThisDeclarationADefinition())
            return classify_record_declaration(tmpl, templated, sm, doc, lang_opts, ns_drop_set, expos_set);
        if (auto diagnostics = record_suppression_diagnostics(tmpl, sm))
            return db::Ignored{std::move(*diagnostics)};
        // Same attached-\verbatim-synopsis substitution, and the same class
        // description, as the plain-record arm above; the docblock is looked
        // up on the ClassTemplateDecl, the decl the arm's other docblock
        // reads use.
        RecordDocblock   block = record_docblock(tmpl, sm);
        db::SynopsisDecl out;
        out.offset        = ev.offset;
        out.synopsis.name = templated->getNameAsString();
        out.diagnostics   = std::move(block.diagnostics);
        out.synopsis.code = block.synopsis ? ir::CodeText{std::move(*block.synopsis), {}}
                                           : extract_synopsis(templated,
                                                              sm,
                                                              lang_opts,
                                                              omit_set,
                                                              expos_set,
                                                              seebelow_map,
                                                              freestanding_map,
                                                              ns_drop_set,
                                                              tmpl);
        collect_inclass_items(templated, sm, lang_opts, ns_drop_set, expos_set, skipped, out.pending, out.diagnostics);
        // Build the roster before grouping so every alias offset still
        // identifies its own routed declaration, including an \also follower.
        out.synopsis.roster = build_roster(templated, sm, expos_set, out.pending);
        group_adjacent_aliases(out.pending);
        attach_class_description(out, std::move(block), templated, sm, lang_opts, ns_drop_set, expos_set);
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
                const unsigned line = document_line(sm, doc, ev.offset);
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
                return db::Ignored{{db::Diagnostic{beman::specgen::Severity::Error,
                                                   line,
                                                   std::move(message),
                                                   diagnostic_file_name(sm, sm.getFileID(ev.decl->getBeginLoc()))}}};
            }
        }
        return db::Ignored{};
    }
    return item_decl_event(std::move(attached), doc);
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

// Join a clang-format-wrapped `{title}` (issue #8): each newline, together
// with the continuation line's indentation and `//` decoration, reads as one
// space, so `{Accumulating applicative instance for\n// expected}` yields
// the title exactly as authored before the formatter wrapped it.
std::string unwrap_title(std::string_view title) {
    std::string out;
    out.reserve(title.size());
    std::size_t pos = 0;
    // substrate generic algorithm: a scan whose newline case consumes a
    // variable-length run (trailing blanks behind, indentation, decoration,
    // and leading blanks ahead) and emits a single joiner.
    while (pos < title.size()) {
        if (title[pos] != '\n') {
            out.push_back(title[pos]);
            ++pos;
            continue;
        }
        // substrate generic algorithm: trim the joiner's left side in place.
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r'))
            out.pop_back();
        ++pos;
        // substrate generic algorithm: skip the continuation's indentation.
        while (pos < title.size() && (title[pos] == ' ' || title[pos] == '\t'))
            ++pos;
        if (pos + 1 < title.size() && title[pos] == '/' && title[pos + 1] == '/') {
            pos += 2;
            if (pos < title.size() && (title[pos] == '/' || title[pos] == '!'))
                ++pos;
            // substrate generic algorithm: skip the blanks after the
            // decoration.
            while (pos < title.size() && (title[pos] == ' ' || title[pos] == '\t'))
                ++pos;
        }
        if (!out.empty() && pos < title.size())
            out.push_back(' ');
    }
    return out;
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
                         // The braced content may span physical lines when a
                         // formatter wrapped a long title (issue #8) — the
                         // comment splitter keeps such continuations in one
                         // item — so the value joins them back into the
                         // authored title.
                         return SectionHeader{depth_stable.first, std::move(depth_stable.second), unwrap_title(title)};
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

            std::size_t after_line = newline == std::string::npos ? raw.size() : newline + 1;
            // A formatter-wrapped `{title}` continues on the following plain
            // `//` lines (issue #8): while the brace is still open, extend
            // this item through each adjacent draft-form continuation — never
            // a `///`/`//!` line or another `\rSec` — so parse_rsec sees the
            // whole marker. A title the RawComment never closes stays
            // unterminated and draws the malformed-\rSec warning as before.
            const auto title_open = [&raw, line_begin](std::size_t end) {
                const std::string_view text(raw.data() + line_begin, end - line_begin);
                const std::size_t      open = text.find('{');
                return open != std::string_view::npos && text.find('}', open) == std::string_view::npos;
            };
            // substrate generic algorithm: the same offset-preserving line
            // scan as the outer loop, consuming continuations until the
            // title closes or the run of plain `//` lines ends.
            while (after_line < raw.size() && title_open(after_line)) {
                const std::size_t next_newline = raw.find('\n', after_line);
                const std::size_t next_end     = next_newline == std::string::npos ? raw.size() : next_newline;
                std::string_view  next(raw.data() + after_line, next_end - after_line);
                const std::size_t lead = next.find_first_not_of(" \t");
                if (lead == std::string_view::npos)
                    break;
                next.remove_prefix(lead);
                if (!next.starts_with("//") || next.starts_with("///") || next.starts_with("//!") ||
                    rsec_tag_recognized(next))
                    break;
                after_line = next_newline == std::string::npos ? raw.size() : next_newline + 1;
            }
            out.push_back(RawItem{raw_begin + static_cast<unsigned>(line_begin),
                                  nullptr,
                                  raw.substr(line_begin, after_line - line_begin)});
            chunk_begin = after_line;
            line_begin  = after_line;
            continue;
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
                                                        const std::map<const clang::Decl*, std::string>& expos_set,
                                                        const clang::VarDecl*             unspecified = nullptr,
                                                        std::optional<clang::SourceRange> see_below   = std::nullopt) {
    const unsigned        decl_begin = sm.getDecomposedLoc(decl->getBeginLoc()).second;
    clang::SourceLocation end_loc    = clang::Lexer::getLocForEndOfToken(decl->getEndLoc(), 0, sm, lang_opts);
    if (const std::optional<clang::Token> semi = clang::Lexer::findNextToken(decl->getEndLoc(), sm, lang_opts);
        semi && semi->is(clang::tok::semi))
        end_loc = clang::Lexer::getLocForEndOfToken(semi->getLocation(), 0, sm, lang_opts);
    const unsigned decl_end = sm.getDecomposedLoc(end_loc).second;

    const llvm::StringRef           buffer = file_buffer(sm, decl->getBeginLoc());
    std::string                     text   = buffer.substr(decl_begin, decl_end - decl_begin).str();
    std::vector<SynopsisEdit>       edits;
    std::map<std::string, SpanInfo> sentinels;
    unsigned                        span_n = 0;

    // A body is never synopsis content (design §3.4).  A region that follows
    // its includes (issue #77) holds function *definitions* -- the header the
    // definitions are written in is the header being specified -- so the
    // splice extract_synopsis does inside a class body is needed here too.
    // `= default` and `= delete` are kept, as they are there.
    //
    // Nothing is edited inside the spliced range, per §3.4: the
    // descending-offset apply loop would take an inner edit first and its
    // overlap watermark would then suppress the splice, which is how a body
    // that named a droppable qualifier survived into the synopsis while a body
    // that named nothing spliced cleanly.
    std::optional<std::pair<unsigned, unsigned>> body_splice;
    if (const clang::FunctionDecl* fn = function_or_template(decl);
        fn != nullptr && fn->doesThisDeclarationHaveABody() && !fn->isDefaulted() && !fn->isDeleted())
        if (const clang::Stmt* body = fn->getBody(); body != nullptr && body->getSourceRange().isValid()) {
            const unsigned body_begin = sm.getDecomposedLoc(body->getBeginLoc()).second;
            if (body_begin > decl_begin && body_begin < decl_end) {
                body_splice = std::pair{body_begin, decl_end};
                edits.push_back(SynopsisEdit{body_begin, decl_end, ";"});
            }
        }
    const auto outside_body = [&](unsigned begin, unsigned end) {
        return !body_splice || end <= body_splice->first || begin >= body_splice->second;
    };

    edits.append_range(
        namespace_qualifier_edits(decl, ns_drop_set, sm, lang_opts, expos_set) |
        std::views::filter([&](const auto& range) {
            return range.first >= decl_begin && range.second <= decl_end && outside_body(range.first, range.second);
        }) |
        std::views::transform([](const auto& range) { return SynopsisEdit{range.first, range.second, ""}; }));
    // substrate generic algorithm: a conditional edit fold over resolved
    // exposition uses; each accepted use can contribute two source edits.
    for (const ExposUse& use : expos_uses(decl, expos_set, sm, lang_opts)) {
        if (use.name_begin < decl_begin || use.name_end > decl_end)
            continue;
        if (!outside_body(use.name_begin, use.name_end))
            continue;
        const std::string sentinel = span_sentinel(span_n++);
        sentinels[sentinel]        = SpanInfo{ir::SpanKind::ExposId, use.display, use.display};
        edits.push_back(SynopsisEdit{use.name_begin, use.name_end, sentinel});
        if (use.qualifier_end > use.qualifier_begin)
            edits.push_back(SynopsisEdit{use.qualifier_begin, use.qualifier_end, ""});
    }

    add_declaration_masks(
        edits, sentinels, span_n, unspecified, see_below, buffer, decl_begin, decl_end, sm, lang_opts);

    const auto*                named    = llvm::dyn_cast<clang::NamedDecl>(decl);
    const clang::FunctionDecl* named_fn = named != nullptr ? llvm::dyn_cast<clang::FunctionDecl>(named) : nullptr;
    if (named != nullptr)
        if (const auto* tmpl = llvm::dyn_cast<clang::FunctionTemplateDecl>(named))
            named_fn = tmpl->getTemplatedDecl();
    // A deduction guide's AST name (`<deduction guide for X>`) is not its
    // source spelling, so an index sentinel would swap the guide's class-name
    // token for that angle-bracketed label (issue #22). A guide renders
    // verbatim and unindexed, as the draft's header synopses show them.
    const bool is_guide = named_fn != nullptr && llvm::isa<clang::CXXDeductionGuideDecl>(named_fn);
    if (named != nullptr && !is_guide && !named->getNameAsString().empty() && named->getLocation().isValid()) {
        clang::SourceRange name_range{named->getLocation(), named->getLocation()};
        if (named_fn != nullptr)
            name_range = named_fn->getNameInfo().getSourceRange();
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

DocumentFiles DocumentFiles::discover(clang::ASTUnit& ast, const clang::SourceManager& sm) {
    DocumentFiles doc;
    doc.main_ = sm.getMainFileID();

    // The gathered region, if the main file opens one: a `\rSec` whose stable
    // name ends in `.syn`, and the matching `/// END` fence.
    const std::map<unsigned, clang::RawComment*>* comments = ast.getASTContext().Comments.getCommentsInFile(doc.main_);
    if (comments == nullptr)
        return doc;

    std::optional<unsigned> region_begin;
    std::string             region_stable;
    std::optional<unsigned> region_end;
    // substrate generic algorithm: a find-then-find over one ordered pass --
    // the opener, then the fence that matches it -- whose state is which of
    // the two is still being looked for.
    for (const auto& [offset, comment] : *comments) {
        const std::string text = comment->getRawText(sm).str();
        if (!region_begin) {
            if (const auto header = parse_rsec(text);
                header && std::string_view(header->value.stable).ends_with(".syn")) {
                region_begin  = offset;
                region_stable = header->value.stable;
            }
            continue;
        }
        if (const auto end = header_synopsis_end(text); end && end->stable == region_stable) {
            region_end = offset + static_cast<unsigned>(end->line_begin);
            break;
        }
    }
    if (!region_begin || !region_end)
        return doc;

    // The files included between them.  A file reached first from somewhere
    // else keeps that first inclusion's location and so is not one of these:
    // the synopsis lists its parts in an order that works, and one that does
    // not is a header the author has to reorder.
    std::vector<clang::FileID> candidates;
    const auto                 note_file = [&](const clang::Decl* decl) {
        if (decl == nullptr || decl->getBeginLoc().isInvalid())
            return;
        const clang::FileID file = sm.getFileID(decl->getBeginLoc());
        if (file.isInvalid() || file == doc.main_)
            return;
        if (!std::ranges::contains(candidates, file))
            candidates.push_back(file);
    };
    // substrate generic algorithm: a for_each-shaped walk driving note_file's
    // side effect over the translation unit's own declaration list.
    for (const clang::Decl* decl : std::ranges::subrange(ast.top_level_begin(), ast.top_level_end()))
        note_file(decl);

    // substrate generic algorithm: a filter-map with a side effect -- a
    // candidate is kept only if its inclusion is the one inside the region,
    // and what is kept is a Part built from the file's buffer.
    for (clang::FileID file : candidates) {
        const clang::SourceLocation include = sm.getIncludeLoc(file);
        if (include.isInvalid())
            continue;
        const auto [include_file, include_offset] = sm.getDecomposedLoc(include);
        if (include_file != doc.main_ || include_offset < *region_begin || include_offset >= *region_end)
            continue;
        doc.parts_.push_back(Part{file, include_offset, static_cast<unsigned>(sm.getBufferData(file).size()), 0});
    }
    std::ranges::sort(doc.parts_, {}, &Part::include_offset);

    unsigned shift = 0;
    // substrate generic algorithm: a running sum assigning each part its base,
    // which is a scan whose state is the sum of the parts before it.
    for (Part& part : doc.parts_) {
        part.base = part.include_offset + 1 + shift;
        shift += part.size + 1;
    }
    return doc;
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

    clang::ASTContext&    ctx = parsed.ast->getASTContext();
    clang::SourceManager& sm  = parsed.ast->getSourceManager();
    // The same document rule build_document uses, so the debugging view shows
    // what the wording is generated from and not a narrower file (issue #77).
    const DocumentFiles doc = DocumentFiles::discover(*parsed.ast, sm);

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
        collect_top_level_decl(decl, sm, doc, decls);
    result.items.append_range(
        decls | std::views::transform([&](clang::Decl* decl) {
            return SourceItem{SourceItem::Kind::Declaration, doc.offset(sm, decl->getBeginLoc()), decl_label(decl)};
        }));

    // The document's raw comments (design §3.1/§3.2), keyed by begin offset;
    // getCommentsInFile may return null when a file has none.
    const auto collect_comments = [&](clang::FileID file) {
        if (const std::map<unsigned, clang::RawComment*>* comments = ctx.Comments.getCommentsInFile(file)) {
            result.items.append_range(*comments | std::views::transform([&](const auto& kv) {
                return SourceItem{
                    SourceItem::Kind::Comment, doc.offset_in(file, kv.first), kv.second->getRawText(sm).str()};
            }));
        }
    };
    collect_comments(doc.main());
    // substrate generic algorithm: a for_each-shaped walk driving
    // collect_comments' side effect over the followed files.
    for (const DocumentFiles::Part& part : doc.parts())
        collect_comments(part.file);

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

    // Which files this document is: the main file, and the ones it `#include`s
    // inside a gathered `.syn` region (issue #77).  Everything below orders and
    // compares *document* offsets, which are the same numbers as before for a
    // document that is one file.
    const DocumentFiles doc = DocumentFiles::discover(*parsed.ast, sm);

    std::vector<clang::Decl*> decls;
    // substrate generic algorithm: same iterator-pair-turned-subrange,
    // for_each-shaped side effect as collect_interleaved's identical walk
    // above.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_top_level_decl(decl, sm, doc, decls);

    // Pre-passes: members to drop from every synopsis
    // (`\omit`/`\merge`) and exposition-only members (`\expos`) to render with an
    // `\exposid` span — both needed before the first extract_synopsis call below.
    // `\expos` alone reads a wider set than the document-structure walk
    // collected: the marked declaration may live in an included header while
    // the signature naming it is here (issue #36).
    std::vector<clang::Decl*> expos_decls = decls;
    // substrate generic algorithm: the same iterator-pair-turned-subrange,
    // for_each-shaped side effect as the walk just above.
    for (clang::Decl* decl : std::ranges::subrange(parsed.ast->top_level_begin(), parsed.ast->top_level_end()))
        collect_expos_scope_decl(decl, sm, main_file, expos_decls);

    const std::set<const clang::Decl*>              omit_set         = build_omit_set(decls, sm);
    const std::map<const clang::Decl*, std::string> expos_set        = build_expos_set(expos_decls, sm);
    const SeeBelowMap                               seebelow_map     = build_seebelow_map(decls, sm);
    const FreestandingMap                           freestanding_map = build_freestanding_map(decls, sm);
    const std::set<std::string>                     ns_drop_set      = build_namespace_drop_set(decls);
    const SkippedRanges                             skipped          = collect_skipped_ranges(*parsed.ast);

    // The docblock ranges some declaration consumed — top-level decls and
    // class members alike, since every comment in the file becomes a RawItem
    // below. classify() consults this for a comment carrying a verbatim
    // marker: attached, the declaration's own event substitutes the authored
    // text, and the comment's standalone node was the duplication (issue #4).
    AttachedCommentRanges attached_comments;
    const auto            note_attached_docblock = [&](const clang::Decl* decl) {
        const clang::RawComment* rc = attached_raw_comment(decl);
        if (rc == nullptr || !is_docblock_comment(rc->getRawText(sm)))
            return;
        const auto [begin_file, begin] = sm.getDecomposedLoc(rc->getBeginLoc());
        const auto [end_file, end]     = sm.getDecomposedLoc(rc->getEndLoc());
        if (begin_file == end_file && doc.contains(begin_file))
            attached_comments.emplace_back(doc.offset_in(begin_file, begin), doc.offset_in(end_file, end));
    };
    // substrate generic algorithm: a for_each-shaped walk driving
    // note_attached_docblock's side effect, over the same decls-plus-members
    // shape the build_omit_set pre-pass walks.
    for (const clang::Decl* decl : decls) {
        note_attached_docblock(decl);
        if (const clang::CXXRecordDecl* record = as_record_decl(decl);
            record != nullptr && record->isThisDeclarationADefinition())
            // substrate generic algorithm: the same side-effecting walk, one
            // level down — a class's members feed the same collector.
            for (const RealRecordMember& member : real_record_members(record))
                note_attached_docblock(member.decl);
    }

    std::vector<RawItem> raw_items;
    raw_items.reserve(decls.size());
    raw_items.append_range(decls | std::views::transform([&](clang::Decl* decl) {
                               return RawItem{doc.offset(sm, decl->getBeginLoc()), decl, {}};
                           }));
    const auto collect_comments = [&](clang::FileID file) {
        if (const std::map<unsigned, clang::RawComment*>* comments = ctx.Comments.getCommentsInFile(file)) {
            // substrate generic algorithm: one RawComment expands to one or
            // more RawItems, so this is a flat-map into the existing event
            // buffer.
            for (const auto& [offset, comment] : *comments)
                append_rsec_comment_items(raw_items, doc.offset_in(file, offset), comment->getRawText(sm).str());
        }
    };
    collect_comments(doc.main());
    // substrate generic algorithm: a for_each-shaped walk driving
    // collect_comments' side effect over the followed files.
    for (const DocumentFiles::Part& part : doc.parts())
        collect_comments(part.file);
    // Same ordering contract as collect_interleaved (design §3.2).
    std::stable_sort(
        raw_items.begin(), raw_items.end(), [](const RawItem& a, const RawItem& b) { return a.offset < b.offset; });

    // The qualifiers ns_drop_set did *not* cover, collected before the
    // pipeline goes clang-free below and attached to the finished document.
    // It is document-level data, so it rides past build_tree rather than
    // through it — no DocEvent carries it and no node owns it.
    const std::vector<ir::ForeignNamespace> foreign = collect_foreign_namespaces(decls, ns_drop_set);

    // The bare-name complement (issue #84), collected the same way and for
    // the same reason: a name whose declaration this run never documents,
    // found by where the reference actually resolves rather than by what the
    // use site wrote.
    const std::vector<ir::ForeignDeclaration> foreign_decls = collect_foreign_declarations(decls, expos_set, sm, doc);

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
        return classify(item,
                        sm,
                        doc,
                        lang_opts,
                        omit_set,
                        expos_set,
                        seebelow_map,
                        freestanding_map,
                        ns_drop_set,
                        skipped,
                        attached_comments);
    };
    const auto boundary_diagnostic = [&](const RawItem& opener, std::string message) {
        const auto [file, local] = doc.locate(opener.offset);
        events.push_back(db::Ignored{{db::Diagnostic{beman::specgen::Severity::Warning,
                                                     sm.getLineNumber(file, local),
                                                     std::move(message),
                                                     diagnostic_file_name(sm, file)}}});
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
        // The furthest end offset of any declaration this fold has consumed.
        // A comment before it sits *inside* that declaration's extracted
        // text — an in-class `\ref` group header, most commonly — and the
        // extraction already carries it, so appending it again as a
        // standalone group header rendered it twice (issue #31).
        unsigned consumed_end = 0;
        // What a folded-in declaration keeps that the gathered node cannot
        // hold. For a class: its class-general paragraph (design §5.2) and its
        // own description (issue #18). Neither is routed -- both belong beside
        // their class's synopsis, in whatever section is open -- and the
        // gathered node has one slot for each while a region may hold several
        // classes, so they travel on as their own events instead of being
        // merged (issue #41). For a namespace entity: its whole wording, when
        // nothing routes it elsewhere (issue #69). Pushed after the gathered
        // node, in source order, which is where their offsets place them
        // anyway.
        std::vector<db::DocEvent> extras;
        // The stable name of the `\ref` group header standing over the
        // declaration being folded, or empty. The in-class equivalent
        // (collect_inclass_items) reads the same headers out of the class
        // body; here the fold walks past them in source order, so the last one
        // seen is the one in force.
        std::string current_ref;
        // substrate generic algorithm: a source-order fold composing semantic
        // CodeText fragments while the outer cursor skips the consumed range.
        for (std::size_t j = i + 1; j <= *close_index; ++j) {
            RawItem item = raw_items[j];
            if (j == *close_index)
                item.comment_text.resize(close->line_begin);

            if (item.decl != nullptr) {
                const clang::SourceLocation decl_end_tok =
                    clang::Lexer::getLocForEndOfToken(item.decl->getEndLoc(), 0, sm, lang_opts);
                consumed_end                              = std::max(consumed_end, doc.offset(sm, decl_end_tok));
                const lowering::ItemDirectives directives = docblock_directives(item.decl, sm);
                db::DocEvent                   classified = classify_one(item);
                if (auto* synopsis = std::get_if<db::SynopsisDecl>(&classified);
                    synopsis != nullptr && !synopsis->synopsis.code.text.empty()) {
                    append_synopsis_code(gathered.synopsis.code, std::move(synopsis->synopsis.code));
                    gathered.diagnostics.append_range(std::move(synopsis->diagnostics));
                    // The class's routed in-class members ride along. They
                    // are not part of the gathered synopsis -- build_tree
                    // scatters each to the `\rSec` its `\ref` names, the
                    // same as for a class outside the region -- but the fold
                    // owns the only event they can travel on, and taking the
                    // code alone dropped them and their descriptions with it
                    // (issue #34), leaving the target section empty.
                    gathered.pending.append_range(std::move(synopsis->pending));
                    // And the coverage roster, which the gathered node used to
                    // carry none of: a class folded into a region was not
                    // coverage-checked at all, and `--validate` could not say
                    // so because the evidence never reached it (issue #45).
                    // Each entry names the class that declared it, so several
                    // classes' entries share this node without a finding
                    // losing track of whose member it is about.
                    gathered.synopsis.roster.append_range(std::move(synopsis->synopsis.roster));
                    // Only the class's own wording travels, in an event built
                    // for it rather than the classified one with its taken
                    // fields left behind: append_range over an rvalue
                    // container copies, so forwarding the whole event would
                    // scatter every routed member and report every finding a
                    // second time. build_tree pushes no node for the synopsis
                    // itself, whose code really has been moved out.
                    if (synopsis->general || !synopsis->descr.elements.empty()) {
                        db::SynopsisDecl class_extra;
                        class_extra.offset  = synopsis->offset;
                        class_extra.general = std::move(synopsis->general);
                        class_extra.descr   = std::move(synopsis->descr);
                        extras.push_back(std::move(class_extra));
                    }
                } else {
                    if (auto* ignored = std::get_if<db::Ignored>(&classified))
                        gathered.diagnostics.append_range(std::move(ignored->diagnostics));
                    if (auto* item_decl = std::get_if<db::ItemDecl>(&classified)) {
                        // A misspelled tag is a typo whether or not the
                        // wording it belongs to finds a home, and this is the
                        // only place left to report it from.
                        gathered.diagnostics.append_range(std::move(item_decl->diagnostics));
                        // The region takes the declaration. It used to take
                        // the description with it and drop it (issue #69) --
                        // silently, and with no roster entry behind it, so
                        // coverage could not report the loss either. A
                        // described entity's wording travels instead, the way
                        // a folded-in class's members do: routed by `\at`,
                        // else by the `\ref` group header standing over it,
                        // to the section that should hold it. A range adaptor
                        // object is the case with no way around this -- a
                        // variable with an initializer has no out-of-line
                        // definition to carry its description into a later
                        // section, and moving its declaration out of the
                        // region would take it out of the header synopsis,
                        // where the draft puts it. Unrouted, the wording rides
                        // out beside the synopsis, which is where it lexically
                        // is. A declaration with nothing to say contributes
                        // only its declaration, exactly as before: a masked
                        // customization point object is the common case.
                        if (!item_decl->item.descr.elements.empty()) {
                            // An out-of-line member definition belongs to its
                            // class's group, not to whatever namespace-scope
                            // header was last seen (issue #77): in the house
                            // style the definition carries the wording, and
                            // its `\ref` group is written in the class body
                            // beside the declaration.
                            std::string inherited = current_ref;
                            if (const clang::FunctionDecl* fn = as_out_of_line_member(item.decl)) {
                                const clang::FunctionDecl* in_class = fn->getFirstDecl();
                                if (const auto* record =
                                        llvm::dyn_cast<clang::CXXRecordDecl>(in_class->getLexicalDeclContext()))
                                    inherited = ClassRefGroups(record, sm, lang_opts)
                                                    .section_for(sm.getDecomposedLoc(in_class->getBeginLoc()).second);
                            }
                            const std::string section = directives.at_anchor.value_or(inherited);
                            if (section.empty()) {
                                extras.push_back(std::move(*item_decl));
                            } else {
                                // The roster entry is what makes a route
                                // checkable: build_tree drops a pending item
                                // whose section no `\rSec` opens, and the
                                // entry is the only record that the request
                                // was made (§9's dangling-route rule, which
                                // reads exactly this). Without it a typo in an
                                // `\at` or a `\ref` header loses the wording
                                // as silently as not routing it at all did,
                                // and a namespace entity has no other roster
                                // entry to be caught by.
                                const auto* named = llvm::dyn_cast<clang::NamedDecl>(item.decl);
                                gathered.synopsis.roster.push_back(ir::SynopsisEntry{
                                    named != nullptr ? named->getNameAsString() : std::string{},
                                    ir::Disposition::Routed,
                                    section,
                                    llvm::isa<clang::FunctionDecl>(item.decl) ? ir::MemberKind::Function
                                                                              : ir::MemberKind::Data});
                                gathered.pending.push_back(
                                    db::PendingItem{section, item_decl->placement_key, std::move(item_decl->item)});
                            }
                        }
                    }
                    if (!directives.omit && !directives.merge && is_first_declaration(item.decl)) {
                        // The marker masks the declaration here exactly as it
                        // does outside the region (issue #55): a
                        // customization point object belongs in the header
                        // synopsis, so a masked variable is inside one by
                        // construction, and that is where the mask stopped
                        // being applied.
                        AttachedItem marked;
                        attach_docblock(marked, item.decl, sm);
                        const VariableMask mask =
                            variable_seebelow_mask(item.decl, marked.directives, marked.grouping_line);
                        std::optional<clang::SourceRange> see_below;
                        if (marked.directives.seebelow && !marked.directives.seebelow_target)
                            see_below = definition_mask_range(item.decl, sm, lang_opts);
                        append_synopsis_code(
                            gathered.synopsis.code,
                            extract_header_declaration(
                                item.decl, sm, lang_opts, ns_drop_set, expos_set, mask.unspecified, see_below));
                    }
                }
                continue;
            }
            if (item.comment_text.empty() || item.offset < consumed_end)
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
                if (line_vocabulary(llvm::StringRef{line}.ltrim(" \t")) == CommentVocabulary::Draft) {
                    if (const std::optional<std::string> ref = parse_ref(line)) {
                        current_ref = *ref;
                        if (!refs.empty())
                            refs.push_back('\n');
                        refs.append(line);
                    }
                }
                if (newline == std::string::npos)
                    break;
                line_begin = newline + 1;
            }
            append_synopsis_code(gathered.synopsis.code, header_comment_code(std::move(refs)));
        }
        events.push_back(std::move(gathered));
        events.append_range(std::move(extras));
        i = *close_index + 1;
    }

    // Stages 2-3: build_tree() folds the `\rSec` frame stack into a tree,
    // grouping each frame's own `\also`/empty-descr followers onto their
    // primaries (via group_items()) in push order as that frame closes —
    // see document_build.hpp's top-of-file note for why stage 3 runs there
    // rather than as a separate pass over the finished tree. Both clang-free,
    // both unit-tested with synthetic events in
    // tests/beman/specgen/document_build.test.cpp.
    db::BuildResult built               = db::build_tree(events);
    built.document.foreign_namespaces   = foreign;
    built.document.foreign_declarations = foreign_decls;
    built.document.unextracted_uses     = body_uses;
    return built;
}

} // namespace beman::specgen::frontend
