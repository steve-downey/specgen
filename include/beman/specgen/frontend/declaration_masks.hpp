// include/beman/specgen/frontend/declaration_masks.hpp           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The parts of a declaration a marker masks (design §4.3): what `\seebelow`
// and `\impdef` replace on an alias, a concept, or a function, as source
// ranges the extraction passes turn into edits.

#ifndef BEMAN_SPECGEN_FRONTEND_DECLARATION_MASKS_HPP
#define BEMAN_SPECGEN_FRONTEND_DECLARATION_MASKS_HPP

#include <beman/specgen/markers.hpp>
#include <beman/specgen/foundation/enumeration.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace beman::specgen::frontend {

// What a bare or targeted `\seebelow` masks on a function (design §4.3).
enum class SeeBelowTarget { ReturnType, Noexcept, Explicit };

// What a targeted `\omit` leaves out of a function's declaration (issue
// #385): what an implementation declares beyond the standard, a strengthened
// exception specification or `= default` on a member the standard declares.
enum class OmitTarget { Noexcept, Default };

// Everything a docblock masks on a function's declaration, wherever it is
// extracted: in a synopsis and in the item declaration alike.
struct DeclarationMask {
    std::optional<SeeBelowTarget> seebelow = std::nullopt;
    std::optional<OmitTarget>     omit     = std::nullopt;

    friend bool operator==(const DeclarationMask&, const DeclarationMask&) = default;
};

using DeclarationMaskMap = std::map<const clang::Decl*, DeclarationMask>;

enum class FreestandingKind { Freestanding, Deleted };
using FreestandingMap = std::map<const clang::Decl*, FreestandingKind>;

enum class AliasMask { SeeBelow, ImplDefined };

std::optional<AliasMask> alias_mask(const grammar::Markers& markers);

// The last token of an alias declaration before its `;`. `getEndLoc()` is the
// end of the *TypeSourceInfo*, which leaves out a trailing cv-qualifier the
// same way a QualifiedTypeLoc leaves out a leading one, so an east-const RHS
// (`using token_type = detail::token const;`) ended at `token`: the itemdecl
// came out with the `const` cut off entirely, and the alias mask left it
// standing beside the placeholder (issue #39, the alias twin of #33). Falls
// back to what the AST said when no `;` follows, rather than running on.
clang::SourceLocation alias_last_token(const clang::TypeAliasDecl* alias,
                                       const clang::SourceManager& sm,
                                       const clang::LangOptions&   lang_opts);

std::optional<clang::SourceRange> alias_rhs_source_range(const clang::TypeAliasDecl* alias,
                                                         const clang::SourceManager& sm,
                                                         const clang::LangOptions&   lang_opts);

// What bare `\seebelow` masks on a namespace-scope entity that is not a
// variable: the *definition*, not the declared type. An alias's right-hand
// side, an alias template's, or a concept's constraint-expression -- the part
// that is implementation and that the draft writes as *see below*. A variable
// masks its declared type instead (issue #24) and goes through `unspecified`.
//
// The alias half already had this on the wording-item path; the concept half
// had it nowhere, and neither had it on the `\expos` standalone-synopsis path,
// so the two markers did not compose and the leak the mask was for was
// reported against a declaration the author had already marked (issue #50).
std::optional<clang::SourceRange>
definition_mask_range(const clang::Decl* decl, const clang::SourceManager& sm, const clang::LangOptions& lang_opts);

std::optional<SeeBelowTarget> seebelow_target(const grammar::Markers& markers);
std::optional<OmitTarget>     omit_target(const grammar::Markers& markers);
// The masks the markers ask for, or nothing when they ask for none.
std::optional<DeclarationMask> declaration_mask(const grammar::Markers& markers);

// The bytes of `fn`'s declaration, in its file, that `target` leaves out: the
// exception specification, or `= default`, with the blanks before it on its
// line. Nothing when the declaration has no such part.
std::optional<std::pair<unsigned, unsigned>> omit_byte_range(const clang::FunctionDecl*  fn,
                                                             OmitTarget                  target,
                                                             const clang::SourceManager& sm,
                                                             const clang::LangOptions&   lang_opts);

// The bytes, in its file, of each parameter name of `fn` that
// `--synopsis-parameter-names drop` leaves out (issue #385), with the blanks
// before it on its line. A name the declaration uses again -- in a noexcept,
// a trailing return type or a requires-clause -- stays, and so does every
// name of a declaration spelled through a macro.
std::vector<std::pair<unsigned, unsigned>> parameter_name_ranges(const clang::FunctionDecl*  fn,
                                                                 const clang::SourceManager& sm,
                                                                 const clang::LangOptions&   lang_opts);

std::optional<clang::SourceRange> seebelow_source_range(const clang::FunctionDecl* fn, SeeBelowTarget target);

} // namespace beman::specgen::frontend

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::frontend::SeeBelowTarget> = std::array{
    beman::specgen::frontend::SeeBelowTarget::ReturnType,
    beman::specgen::frontend::SeeBelowTarget::Noexcept,
    beman::specgen::frontend::SeeBelowTarget::Explicit,
};

template <>
inline constexpr auto beman::specgen::foundation::enumerators<beman::specgen::frontend::OmitTarget> = std::array{
    beman::specgen::frontend::OmitTarget::Noexcept,
    beman::specgen::frontend::OmitTarget::Default,
};

#endif // BEMAN_SPECGEN_FRONTEND_DECLARATION_MASKS_HPP
