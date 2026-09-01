# Architecture decision records

This directory holds one record per settled structural or stylistic decision in
`beman.specgen`: the decision itself, the context that forces the choice, and the
consequences that follow from it. Each record is written in the present tense —
it describes the project as it stands — and records marked *Accepted (amended)*
carry a note narrowing the original scope. Records cite each other by file name
and cite the design document as `docs/architecture.md` §N.

## Contents

| Record | Summary |
| --- | --- |
| [tool-not-library](tool-not-library.md) | An executable, not a consumable library |
| [ir-boundary](ir-boundary.md) | `ir::Document` is the front-end/backend contract |
| [clang-free-render-mode](clang-free-render-mode.md) | `render --from-ir` invokes no Clang at run time |
| [component-recipe](component-recipe.md) | Seven mechanical steps to add a component |
| [golden-test-harness](golden-test-harness.md) | Byte-exact goldens; regeneration is reviewed |
| [hermetic-corpus](hermetic-corpus.md) | Curated corpus; no submodules, no vendored draft |
| [llvm-toolchain-pin](llvm-toolchain-pin.md) | `BEMAN_SPECGEN_LLVM_VERSION` pins the LLVM |
| [subtree-consumption](subtree-consumption.md) | Git subtree or ported, provenance-tracked code |
| [node-base-functor](node-base-functor.md) | `NodeF<A>` + `fold_with`; `ir.hpp` untouched |
| [backend-direct-algebra](backend-direct-algebra.md) | A backend is a fold algebra straight to output |
| [json-single-schema](json-single-schema.md) | One descriptor table drives JSON emit and parse |
| [parser-combinators](parser-combinators.md) | Combinators replace the hand scanners |
| [expected-error-taxonomy](expected-error-taxonomy.md) | Expected-based errors; diagnostics as a monoid |
| [visitation-rules](visitation-rules.md) | Tripwire `overloaded`; named structs when stateful |
| [marker-registry](marker-registry.md) | One `constexpr` marker table |
| [document-build-stages](document-build-stages.md) | Classify → build-tree → group pipeline |
| [no-typeclass-objects](no-typeclass-objects.md) | Explicit operation parameters; no lookup tier |
| [cxx26-baseline](cxx26-baseline.md) | C++26 floor; GCC 16, Clang 22–23, libstdc++ |
| [format-print-output](format-print-output.md) | `std::format`/`std::print`; streams move built bytes |
