// tests/corpus/spec_synopsis_comments.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A synopsis keeps the comments it recognizes or the author marks, and drops
// the rest (issue #280). Every plain `//` comment in a class body used to be
// synopsis text, which made a class in an annotated header a place where an
// implementation comment could not be written: the only ways out were `///`,
// which hands Doxygen an implementation note as the documentation of the next
// declaration, or moving the comment out of the class, away from what it
// explains.
//
// Kept:
//
//  - `// \ref{x}, title`, a group header, as always;
//  - a comment that starts `\syn`, printed without the marker. It stays an
//    ordinary comment to the compiler, to Doxygen and to clang-format. On a
//    line of its own (`// \syn access functions`), after a declaration, and
//    as a `/* */` comment;
//  - at namespace scope in a gathered header synopsis, the same two.
//
// Dropped: every other plain comment, on a line of its own or after a
// declaration, `/* */` included, and the comment left behind above an
// `\omit`ted member, which used to survive without its declaration.
// Docblocks and Doxygen are dropped as they always were.
//
// `--keep-synopsis-comments` is the earlier rule, pinned by a unit test over
// this header: every plain comment stays, a marked one still without its
// marker.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_COMMENTS_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_COMMENTS_HPP

namespace demo {

// \rSec2[demo.tray.syn]{Header synopsis}

// An implementation note at namespace scope.
// \syn forward declarations
class tray;

// \ref{demo.tray.ops}, operations
void tidy(tray&);

/// END [demo.tray.syn]

// \rSec2[demo.tray]{Class tray}

class tray {
  public:
    // \ref{demo.tray.cons}, constructors
    // Implementation note: the trivial path is unconstrained on purpose, so
    // that it is the sole declaration when the element is not copyable.
    tray();

    // Rationale for an implementation-only member.
    //! \omit
    void impl();

    // \syn access functions
    // \ref{demo.tray.obs}, observers
    bool empty() const; // never throws in practice
    int  size() const;  // \syn see below
    /* \syn the count of elements */
    int count(int /* unused */) const;

    /// Doxygen for the next member, which a synopsis never printed.
    int capacity() const;
    /* An implementation note in a block comment. */

  private:
    //! \expos
    int size_ = 0;
};

// \rSec3[demo.tray.cons]{Constructors}

//! \effects Constructs an empty tray.
inline tray::tray() {}

// \rSec3[demo.tray.obs]{Observers}

//! \returns `size() == 0`.
inline bool tray::empty() const { return size_ == 0; }

//! \returns The number of elements.
inline int tray::size() const { return size_; }

//! \returns The number of elements, whatever the argument.
inline int tray::count(int) const { return size_; }

//! \returns The number of elements the tray can hold.
inline int tray::capacity() const { return size_; }

// \rSec3[demo.tray.ops]{Operations}

//! \effects Tidies `t`.
inline void tidy(tray& t) { static_cast<void>(t); }

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SYNOPSIS_COMMENTS_HPP
