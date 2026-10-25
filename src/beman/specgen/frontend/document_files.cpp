// src/beman/specgen/frontend/document_files.cpp                  -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/specgen/frontend/document_files.hpp>

#include <beman/specgen/rsec.hpp>
#include <beman/specgen/section_markers.hpp>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Specifiers.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace beman::specgen::frontend {

using grammar::parse_rsec;
using grammar::section_end_fence;

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
        if (const auto end = section_end_fence(text); end && end->stable == region_stable) {
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

unsigned document_line(const clang::SourceManager& sm, const DocumentFiles& doc, unsigned document_offset) {
    const auto [file, local] = doc.locate(document_offset);
    return sm.getLineNumber(file, local);
}

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

} // namespace beman::specgen::frontend
