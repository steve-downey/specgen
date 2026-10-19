// tests/corpus/spec_source_markup.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Members declared here and defined in spec_source_markup.cc, named with
// `--source`, take their wording from the definitions there (issue #149),
// placed by this class's `\ref` groups as a definition in this header would
// be. The source file's parse derives what a body supplies: `assign`'s
// Mandates from its static_assert, `reset`'s Equivalent to, and `size`'s
// Returns. Its markers reach this synopsis too: `data`'s `\seebelow` masks the
// return type, and `rehash`'s `\omit` drops the line. `operator==` is a
// friend whose definition is in the source file.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_SOURCE_MARKUP_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_SOURCE_MARKUP_HPP

namespace demo {

class tally {
  public:
    // \ref{tally.modifiers}, modifiers
    void reset();
    void assign(long value);
    void rehash();

    // \ref{tally.observers}, observers
    int  size() const;
    int* data();

    // \ref{tally.ops}, operations
    friend bool operator==(const tally& x, const tally& y);

  private:
    //! \expos
    int count_ = 0;
};

// \rSec2[tally]{Class tally}

// \rSec3[tally.modifiers]{Modifiers}

// \rSec3[tally.observers]{Observers}

// \rSec3[tally.ops]{Operations}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_SOURCE_MARKUP_HPP
