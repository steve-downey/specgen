// include/beman/specgen/validate/validate.hpp                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// specgen — IR validators (decision expected-error-taxonomy).
// A validator is `fold_with<Diagnostics>` over `ir::NodeF` (ir_fold.hpp):
// no inherited attribute is resolved during descent, which is the case
// `backend/common.hpp`'s rule assigns to `ir::NodeF`/`node_project` rather
// than `backend::common::RenderF` -- a diagnostic has no header text that
// needed rendering on the way down. Findings accumulate under the
// diagnostics monoid instead of failing fast: a malformed span table three
// sections deep should not hide every other finding in the document.
//
// The rules themselves are `ValidationAlgebra` cases in validate.cpp; this
// header does not change as rules are added.
#ifndef BEMAN_SPECGEN_VALIDATE_VALIDATE_HPP
#define BEMAN_SPECGEN_VALIDATE_VALIDATE_HPP

#include <beman/specgen/diagnostic.hpp>
#include <beman/specgen/foundation/monoid.hpp>
#include <beman/specgen/ir.hpp>

#include <string>
#include <vector>

namespace beman::specgen::validate {

/// One validator finding. `context` is a `/`-joined path built from the
/// stable names of the sections enclosing the node the finding is about
/// (outermost first); a section with an empty `stable_name` contributes no
/// segment. A rule that reports about something *no node holds* builds its
/// own context in the same shape instead -- the body-use rule's
/// `optional::optional/body` names a function body, which is code the
/// document does not contain.
/// `message` is the house diagnostic voice: lower-case, no trailing period.
struct Diagnostic {
    Severity    severity;
    std::string context;
    std::string message;

    // Hidden friend (CODING_RULES' one exception to out-of-line
    // definitions): a customization point, not ordinary behavior, and short
    // enough to read at the declaration.
    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};
using Diagnostics = std::vector<Diagnostic>;

// The diagnostics monoid (decision expected-error-taxonomy): combine is
// concatenation (left operand's findings first, order preserved), identity
// is the empty vector.
inline const auto diagnostics_monoid = foundation::monoid{
    [](Diagnostics a, const Diagnostics& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    },
    Diagnostics{},
};

/// Fold @p node under `ValidationAlgebra` (validate.cpp), collecting every
/// validator finding for it and its descendants.
Diagnostics validate(const ir::Node& node);

/// `ir::Document` is a forest, not a single tree (ir_fold.hpp says so): fold
/// each root in `Document::nodes` and combine under the monoid, the same
/// walk `ir.cpp`'s `emit_json(Document&)` does directly over the roots.
Diagnostics validate(const ir::Document& document);

/// True iff @p diagnostics contains at least one `Severity::Error` finding.
bool has_errors(const Diagnostics& diagnostics);

/// Renders one finding as `"<context>: <severity>: <message>"` -- the same
/// `<location>: <level>: text` shape every other driver message uses
/// (tools/specgen/main.cpp), so warning and error text cannot drift apart.
std::string format_diagnostic(const Diagnostic& diagnostic);

} // namespace beman::specgen::validate

#endif // BEMAN_SPECGEN_VALIDATE_VALIDATE_HPP
