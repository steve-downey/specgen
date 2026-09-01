// tests/corpus/spec_doxygen_only.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Hand-curated corpus header (decision hermetic-corpus) for the second
// gate: a header documented **entirely in Doxygen**, with no specgen markup
// anywhere. `spec_doxygen.hpp` beside it is the mixed case — Doxygen added to
// a header that was written for specgen — and this one is the case specgen
// will actually meet, because it is how a library that has never heard of
// specgen is documented.
//
// What is being asserted is a *robustness* property rather than a wording
// one, and the distinction matters because the wording here is empty on
// purpose. Pointing the tool at a header like this must:
//
//   - parse and exit 0, with no diagnostic (`golden.doxygen_only` is a
//     generate case, so a nonzero exit or a crash fails it),
//   - emit IR that reads back and renders (the `.roundtrip` sibling), and
//   - describe nothing, and *say so* — `golden.doxygen_only_validate` pins
//     one design §9 coverage Error per declaration. That is the tool
//     reporting that the header carries no wording, which is true; the
//     failure mode this rules out is the tool quietly turning `@brief` prose
//     into an Effects paragraph.
//
// The case is registered `NO_VALIDATE` for the reason `spec_namespace.hpp`
// establishes: a fixture written to violate a validator pins the
// finding instead of skipping the check.
//
// The Doxygen surface below is deliberately wider than the real
// `beman/optional` header's, which uses `///` and `/** */` blocks with
// `@brief`/`@param`/`@return` and nothing else: `///<` trailing comments,
// an `@code`/`@endcode` block whose braces and `//` would confuse anything
// scanning comment text as code, a `@{`/`@}` member group, and `\ingroup`
// in backslash rather than at-sign form all appear here and not there.
//
// **One Doxygen spelling is deliberately absent.** Doxygen accepts four
// comment forms and specgen has claimed two of them — `//!` and `/*!` are
// specgen's markup, and a header using those *for Doxygen* is ambiguous by
// construction, not a case this fixture can speak to. See AGENTS.md.
//
// Self-contained (no #includes) and built from bool/int only, so it parses
// standalone under -std=c++2c.

#ifndef BEMAN_SPECGEN_CORPUS_SPEC_DOXYGEN_ONLY_HPP
#define BEMAN_SPECGEN_CORPUS_SPEC_DOXYGEN_ONLY_HPP

namespace demo {

/**
 * @brief A small value type, documented the way a library that has never
 * heard of specgen documents one.
 *
 * \ingroup demo_types
 *
 * @code
 *   gadget g{7};        // braces and a comment, inside a comment
 *   if (!g.empty()) { return g.value(); }
 * @endcode
 */
class gadget {
  public:
    /** @name Construction */
    /** @{ */

    /// @brief Constructs an empty gadget.
    gadget();

    /**
     * @brief Constructs a gadget holding a value.
     * @param value The value to hold.
     */
    explicit gadget(int value);

    /** @} */

    /**
     * @brief Whether the gadget holds no value.
     * @return `true` when empty, `false` otherwise.
     */
    bool empty() const;

    /// @brief The held value.
    /// @return The value passed at construction, or zero.
    int value() const;

  private:
    int value_ = 0; ///< The held value, or zero.
};

/**
 * @brief Compares two gadgets.
 * @param a The left operand.
 * @param b The right operand.
 * @return `true` when both hold the same value.
 */
bool operator==(const gadget& a, const gadget& b);

} // namespace demo

#endif // BEMAN_SPECGEN_CORPUS_SPEC_DOXYGEN_ONLY_HPP
