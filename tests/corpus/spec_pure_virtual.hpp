// tests/corpus/spec_pure_virtual.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A pure virtual member needs no definition, so its declaration is the only
// place its markup can be written (issue #140). A documented one is attached
// from the declaration the way a `= default`/`= delete` member is: the itemdecl
// keeps the `= 0` tail, the way the draft writes `memory_resource`'s
// virtuals. An undocumented one has nothing to say, and a marker-only docblock
// asks for nothing, so neither earns an itemdecl.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_PURE_VIRTUAL_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_PURE_VIRTUAL_HPP

namespace demo {

class resource {
  public:
    virtual ~resource() = default;

    // \ref{resource.virtuals}, virtual member functions
    //! \effects Allocates storage of at least `bytes` bytes.
    //! \returns A pointer to the allocated storage.
    virtual void* do_allocate(unsigned bytes) = 0;

  private:
    //! \expos
    virtual void do_reset() = 0;
};

// \rSec3[resource.virtuals]{Virtual member functions}

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_PURE_VIRTUAL_HPP
