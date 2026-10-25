// include/beman/specgen/frontend/document_files.hpp              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Which files and which declarations make up one document (design §3.1,
// issue #77): the main file and the files a `.syn` region includes, mapped
// onto one run of document offsets, and the top-level declarations collected
// from them.

#ifndef BEMAN_SPECGEN_FRONTEND_DOCUMENT_FILES_HPP
#define BEMAN_SPECGEN_FRONTEND_DOCUMENT_FILES_HPP

#include <clang/AST/DeclBase.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

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

// The line a document offset is on, in the file it is actually in: a line
// number in the virtual concatenation is a line in no file anyone can open.
unsigned document_line(const clang::SourceManager& sm, const DocumentFiles& doc, unsigned document_offset);

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
                            std::vector<clang::Decl*>&  out);

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
                              std::vector<clang::Decl*>&  out);

} // namespace beman::specgen::frontend

#endif // BEMAN_SPECGEN_FRONTEND_DOCUMENT_FILES_HPP
