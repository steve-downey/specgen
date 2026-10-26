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
// that namespace `std` by dropping the implementation's own. Each span moves
// with the character it starts on; an end moves with the last character it
// covers, so the indentation added at the start of the next line stays
// outside it.
void enclose_in_namespace_std(beman::specgen::ir::CodeText& code);

void append_synopsis_code(beman::specgen::ir::CodeText& out, beman::specgen::ir::CodeText part);

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
                                                        std::optional<clang::SourceRange> see_below   = std::nullopt);

beman::specgen::ir::CodeText header_comment_code(std::string text);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_HEADER_SYNOPSIS_HPP
