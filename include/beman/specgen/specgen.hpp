// include/beman/specgen/specgen.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_SPECGEN_SPECGEN_HPP
#define BEMAN_SPECGEN_SPECGEN_HPP

#include <beman/specgen/config.hpp>

#if BEMAN_SPECGEN_USE_MODULES() && !defined(BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT)

import beman.specgen;

#else

    #include <beman/specgen/backend/common.hpp>
    #include <beman/specgen/backend/latex.hpp>
    #include <beman/specgen/backend/mpark.hpp>
    #include <beman/specgen/backend/org.hpp>
    #include <beman/specgen/conjuncts.hpp>
    #include <beman/specgen/diagnostic.hpp>
    #include <beman/specgen/docblock.hpp>
    #include <beman/specgen/document_build.hpp>
    #include <beman/specgen/foundation/fold_left_short.hpp>
    #include <beman/specgen/foundation/json_descriptor.hpp>
    #include <beman/specgen/foundation/json_writer.hpp>
    #include <beman/specgen/foundation/monoid.hpp>
    #include <beman/specgen/foundation/overloaded.hpp>
    #include <beman/specgen/foundation/parse/cursor.hpp>
    #include <beman/specgen/foundation/parse/parser.hpp>
    #include <beman/specgen/foundation/traverse.hpp>
    #include <beman/specgen/fragments.hpp>
    #include <beman/specgen/ir.hpp>
    #include <beman/specgen/ir_fold.hpp>
    #include <beman/specgen/lower.hpp>
    #include <beman/specgen/markers.hpp>
    #include <beman/specgen/validate/validate.hpp>

#endif // BEMAN_SPECGEN_USE_MODULES() &&
       // !defined(BEMAN_SPECGEN_INCLUDED_FROM_INTERFACE_UNIT)

#endif // BEMAN_SPECGEN_SPECGEN_HPP
