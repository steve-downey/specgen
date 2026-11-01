// include/beman/specgen/frontend/header_synopsis.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// A gathered header synopsis (design §3.4): the `.syn` region's
// declarations and structure comments, assembled into one synopsis inside
// `namespace std`.

#ifndef BEMAN_SPECGEN_FRONTEND_HEADER_SYNOPSIS_HPP
#define BEMAN_SPECGEN_FRONTEND_HEADER_SYNOPSIS_HPP

#include <beman/specgen/document_build.hpp>
#include <beman/specgen/ir.hpp>
#include <beman/specgen/frontend/attach.hpp>
#include <beman/specgen/frontend/build_failure.hpp>
#include <beman/specgen/frontend/compile_args.hpp>
#include <beman/specgen/frontend/declaration_masks.hpp>
#include <beman/specgen/frontend/document_files.hpp>
#include <beman/specgen/frontend/draft_format.hpp>
#include <beman/specgen/frontend/file_views.hpp>
#include <beman/specgen/frontend/classify.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>

#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// Clang coalesces adjacent line comments into one RawComment, but each
// physical \rSec or END line is a distinct structure event. Split only around
// those lines: the intervening chunks stay intact so multi-line docblocks and
// other comment consumers retain their existing input shape. Offsets remain
// file offsets rather than offsets into the merged RawComment.
void append_structure_comment_items(std::vector<RawItem>& out, unsigned raw_begin, std::string raw);

// The draft writes every synopsis at namespace scope inside the namespace
// it belongs to -- `namespace std {`, its body indented two spaces, then the
// closing brace -- and the namespace mapping (design §3.5) has already made
// that namespace `std` by dropping the implementation's own, or `std::whatwg`
// for a class in a specified nested namespace (issue #209). `name` is written
// after the keyword as given, so a nested-namespace-definition
// (`std::inline literals`) is the caller's spelling. Each span moves with the
// character it starts on; an end moves with the last character it covers, so
// the indentation added at the start of the next line stays outside it.
// Enclosures compose: a block enclosed in `views` and then, with its
// siblings, in `std` is indented twice.
void enclose_in_namespace(beman::specgen::ir::CodeText& code, std::string_view name);

void append_synopsis_code(beman::specgen::ir::CodeText& out, beman::specgen::ir::CodeText part);

// A gathered header synopsis's code, assembled by nested namespace (design
// §3.5, issue #209). Consecutive declarations in one specified namespace form
// a run; a run below `std` is enclosed in its `namespace views { ... }`
// block, which is [ranges.syn]'s shape, and a change of namespace closes the
// run and opens the next, which is [time.syn]'s, in source order. A structure
// comment -- a `\ref` group header, a verbatim payload -- is deferred to the
// run of the declaration that follows it, where the draft writes a group
// header, else to the top level at the end.
//
// Clang-free, so the open/close sequence is unit-tested on synthetic
// fragments rather than through a parse.
class NamespaceRuns {
  public:
    // A declaration's code, in the nested namespace `definition` names;
    // empty for `std` itself.
    void append(beman::specgen::ir::CodeText part, std::string_view definition);
    void append_comment(beman::specgen::ir::CodeText part);
    // The assembled code; the runs are closed and the object is spent.
    beman::specgen::ir::CodeText finish();

  private:
    void flush();

    beman::specgen::ir::CodeText out_;
    beman::specgen::ir::CodeText run_;
    beman::specgen::ir::CodeText deferred_;
    std::string                  current_;
};

// A declaration that ordinary classification intentionally ignores still
// belongs in a bounded header synopsis. Extract it through its semicolon,
// applying the same qualifier, exposition-use, and index sentinel machinery
// as the established class and namespace-exposition paths. `indent` is how
// far the text is indented once enclosed (draft_format.hpp): one level
// inside `namespace std {`, two inside a nested namespace block.
beman::specgen::ir::CodeText extract_header_declaration(clang::Decl*                                     decl,
                                                        const clang::SourceManager&                      sm,
                                                        const clang::LangOptions&                        lang_opts,
                                                        const std::set<std::string>&                     ns_drop_set,
                                                        const std::map<const clang::Decl*, std::string>& expos_set,
                                                        const clang::VarDecl*             unspecified = nullptr,
                                                        std::optional<clang::SourceRange> see_below   = std::nullopt,
                                                        unsigned                          indent = kNamespaceIndent);

beman::specgen::ir::CodeText header_comment_code(std::string text);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_HEADER_SYNOPSIS_HPP
