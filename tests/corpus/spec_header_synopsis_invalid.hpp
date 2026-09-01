// tests/corpus/spec_header_synopsis_invalid.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// \rSec2[broken.syn]{Broken synopsis}

struct before_mismatch {};

/// END [different.syn]

// \rSec2[after.mismatch]{After mismatch}

struct after_mismatch {};

// \rSec2[unclosed.syn]{Unclosed synopsis}

struct before_nested {};

// \rSec2[after.nested]{After nested section}

struct after_nested {};
