// include/beman/specgen/foundation/parse/parser.hpp             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Provenance: ported from compile-time-scheme
//   repo:   https://github.com/steve-downey/compile-time-scheme (commit 4b0b216)
//   path:   src/smd/smdscheme/parser/parser.hpp, alt.hpp
// Runtime-ized (decision parser-combinators):
//   - foundation::result<T> (upstream's carrier, foundation/result.hpp) is
//     replaced by std::expected<T, E> (specgen's carrier, DEC-1 in
//     foundation/DIVERGENCES.md); parse_result<T> is the specialization.
//   - The Capacity NTTP and foundation::static_vector are dropped from
//     many/some; both collect into std::vector.
//   - keyword() is new: a whole-spelling matcher built from the same
//     char-at-a-time, commit-on-consumed-input discipline as char_p, needed
//     by specgen's marker grammars (\rSec, \ref, docblock tags) that upstream
//     has no equivalent for (upstream matches single Scheme datums, never a
//     fixed multi-character spelling).
//   - digits() is new: a checked, allocation-free unsigned-integer parser
//     using std::from_chars, added specifically so parse_rsec
//     can report an out-of-range \rSec depth as a positioned parse failure
//     instead of a latent std::stoi throw.
//   - No consteval requirement anywhere: specgen is runtime-only, so
//     the typeclass-object layer (parser_ops.hpp, upstream's Layer 2) is not
//     ported at all (decision no-typeclass-objects: explicit-parameter tier
//     only).
// The applicative/alternative combinators below (pure, satisfy, char_p, map,
// lift2, sequence_left, sequence_right, operator|, many, some, opt, lexeme)
// otherwise carry upstream's shape and doc comments. See
// foundation/DIVERGENCES.md.
#ifndef BEMAN_SPECGEN_FOUNDATION_PARSE_PARSER_HPP
#define BEMAN_SPECGEN_FOUNDATION_PARSE_PARSER_HPP

#include <beman/specgen/foundation/parse/cursor.hpp>

#include <charconv>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

namespace beman::specgen::foundation::parse {

/// A parse failure with the position in the input where it occurred and a
/// static string describing the expected token or form.
///
/// The @p message pointer must be a string literal or have static lifetime;
/// the struct does not own or copy the pointed-to string. The position
/// doubles as the ordered-choice commit signal (decision parser-combinators):
/// @c operator|
/// only tries its second alternative when the first failed *without*
/// advancing past its starting position.
struct parse_error {
    source_pos  where{};   ///< Position of the failure in the input.
    const char* message{}; ///< Static description of what was expected.
};

/// Concept satisfied by any callable @p P that can be invoked with a @ref
/// cursor.
template <class P>
concept parser_like = requires(P p, cursor c) { p(c); };

/// Carries a successfully parsed value together with the unconsumed @ref
/// cursor.
///
/// @tparam T The type of the parsed value.
template <class T>
struct parse_state {
    T      value; ///< The successfully parsed value.
    cursor rest;  ///< The cursor positioned after the parsed input.
};

/// Alias for the result type returned by all parsers: either a @ref
/// parse_state on success or a @ref parse_error on failure. std::expected is
/// specgen's carrier (decision expected-error-taxonomy, DEC-1 in
/// foundation/DIVERGENCES.md).
///
/// @tparam T The success value type.
template <class T>
using parse_result = std::expected<parse_state<T>, parse_error>;

/// A type-erased callable wrapper for a single-pass parser.
///
/// @c parser<F> wraps a callable @p F with signature
/// @c parse_result<T>(cursor). It satisfies @ref parser_like so it composes
/// with all the combinator functions in this header.
///
/// @tparam F Callable type; deduced via the deduction guide.
template <class F>
class parser {
  public:
    /// Constructs a parser wrapping @p f.
    constexpr explicit parser(F f) : f_{f} {}

    /// Runs the parser starting at @p cur.
    constexpr auto operator()(cursor cur) const { return f_(cur); }

  private:
    F f_;
};

/// Deduction guide: @c parser(f) deduces @c parser<F>.
template <class F>
parser(F) -> parser<F>;

/// Returns a parser that always succeeds, consuming no input and yielding
/// @p value.
///
/// @tparam T Value type.
/// @param  value The constant value to produce.
template <class T>
[[nodiscard]] constexpr auto pure(T value) {
    return parser{[v = value](cursor cur) -> parse_result<T> { return parse_state<T>{v, cur}; }};
}

/// Returns a parser that succeeds when the next character satisfies @p pred.
///
/// On success consumes one character. On failure reports @p expected at the
/// current position.
///
/// @param pred     Predicate on @c char.
/// @param expected Human-readable description of the expected token (used in
///                 @ref parse_error::message).
[[nodiscard]] constexpr auto satisfy(auto pred, const char* expected) {
    return parser{[pred, expected](cursor cur) -> parse_result<char> {
        if (!cur.empty() && pred(cur.peek())) {
            return parse_state<char>{cur.peek(), cur.bump()};
        }
        return std::unexpected(parse_error{cur.position(), expected});
    }};
}

/// Returns a parser that matches exactly the character @p expected.
///
/// @param expected The character to match.
[[nodiscard]] constexpr auto char_p(char expected) {
    return satisfy([expected](char c) { return c == expected; }, "expected char");
}

/// Returns a parser that matches the literal spelling @p keyword_text,
/// character by character.
///
/// A mismatch on the *first* character fails without having consumed input,
/// so @c operator| can still try a sibling alternative that shares no
/// prefix. A mismatch after one or more characters have matched fails
/// *having consumed* input, which — per the commit-on-consumed-input rule —
/// makes that failure final: a caller matching `\rSec` against input that
/// starts `\rS` but not `\rSec` gets a definite error naming what was
/// expected, not a silent fall-through to an unrelated alternative.
///
/// @param keyword_text The exact spelling to match; must outlive every use of
///                      the returned parser (a string_view is captured).
[[nodiscard]] constexpr auto keyword(std::string_view keyword_text) {
    return parser{[keyword_text](cursor cur) -> parse_result<std::string_view> {
        for (char expected : keyword_text) { // substrate generic algorithm
            if (cur.empty() || cur.peek() != expected) {
                return std::unexpected(parse_error{cur.position(), "expected keyword"});
            }
            cur = cur.bump();
        }
        return parse_state<std::string_view>{keyword_text, cur};
    }};
}

/// Returns a parser that applies @p f to the result of @p pa.
///
/// Fails with @p pa's error if @p pa fails; the error position is preserved
/// so the caller can decide whether to try alternatives.
///
/// @tparam PA Parser type.
/// @tparam F  Callable to apply to the parse value.
template <parser_like PA, class F>
[[nodiscard]] constexpr auto map(PA pa, F f) {
    return parser{[pa, f](cursor cur) {
        auto r  = pa(cur);
        using R = decltype(f(r->value));
        if (!r.has_value()) {
            return parse_result<R>{std::unexpected(r.error())};
        }
        return parse_result<R>{parse_state<R>{f(r->value), r->rest}};
    }};
}

/// Returns a parser that runs @p pa then @p pb in sequence, combining their
/// values with @p f.
///
/// Fails if either @p pa or @p pb fails, forwarding the earliest error.
///
/// @tparam PA Parser for the first value.
/// @tparam PB Parser for the second value.
/// @tparam F  Binary combiner callable.
template <parser_like PA, parser_like PB, class F>
[[nodiscard]] constexpr auto lift2(PA pa, PB pb, F f) {
    return parser{[pa, pb, f](cursor cur) {
        auto ra = pa(cur);
        using V = decltype(f(ra->value, pb(cur)->value));
        if (!ra.has_value()) {
            return parse_result<V>{std::unexpected(ra.error())};
        }
        auto rb = pb(ra->rest);
        if (!rb.has_value()) {
            return parse_result<V>{std::unexpected(rb.error())};
        }
        return parse_result<V>{parse_state<V>{f(ra->value, rb->value), rb->rest}};
    }};
}

/// Returns a parser that runs @p pa then @p pb, discarding @p pb's value.
template <parser_like PA, parser_like PB>
[[nodiscard]] constexpr auto sequence_left(PA pa, PB pb) {
    return lift2(pa, pb, [](auto a, auto) { return a; });
}

/// Returns a parser that runs @p pa then @p pb, discarding @p pa's value.
template <parser_like PA, parser_like PB>
[[nodiscard]] constexpr auto sequence_right(PA pa, PB pb) {
    return lift2(pa, pb, [](auto, auto b) { return b; });
}

/// Ordered-choice combinator: tries @p pa; if it fails *at the same position*
/// it started, tries @p pb.
///
/// If @p pa consumes input before failing, the error is propagated without
/// trying @p pb — the commit-on-consumed-input rule (decision parser-combinators):
/// once a branch has eaten input, its failure is final, which prevents
/// accidental backtracking into already-consumed tokens and lets a partial
/// keyword match report a precise error instead of silently trying an
/// unrelated sibling.
///
/// @tparam PA First alternative parser.
/// @tparam PB Second alternative parser.
template <parser_like PA, parser_like PB>
[[nodiscard]] constexpr auto operator|(PA pa, PB pb) {
    return parser{[pa, pb](cursor cur) {
        auto start = cur.position().offset;
        auto ra    = pa(cur);
        if (ra.has_value())
            return ra;
        if (ra.error().where.offset != start)
            return ra;
        return pb(cur);
    }};
}

/// Returns a parser that applies @p p zero or more times, collecting every
/// result into a @c std::vector.
///
/// Always succeeds (zero repetitions is valid).
///
/// @pre @p p does not succeed without consuming input; otherwise this loops
///      forever. None of the primitives in this header have that shape.
/// @tparam P Parser to repeat.
template <parser_like P>
[[nodiscard]] constexpr auto many(P p) {
    return parser{[p](cursor cur) {
        using V = decltype(p(cur)->value);
        std::vector<V> result{};
        while (true) { // substrate generic algorithm
            auto r = p(cur);
            if (!r.has_value())
                break;
            result.push_back(std::move(r->value));
            cur = r->rest;
        }
        return parse_result<std::vector<V>>{parse_state<std::vector<V>>{std::move(result), cur}};
    }};
}

/// Returns a parser that applies @p p one or more times, collecting every
/// result into a @c std::vector.
///
/// Fails if @p p does not match at least once.
///
/// @tparam P Parser to repeat.
template <parser_like P>
[[nodiscard]] constexpr auto some(P p) {
    return parser{[p](cursor cur) {
        using V    = decltype(p(cur)->value);
        auto first = p(cur);
        if (!first.has_value()) {
            return parse_result<std::vector<V>>{std::unexpected(first.error())};
        }
        std::vector<V> result{};
        result.push_back(std::move(first->value));
        cur = first->rest;
        while (true) { // substrate generic algorithm
            auto r = p(cur);
            if (!r.has_value())
                break;
            result.push_back(std::move(r->value));
            cur = r->rest;
        }
        return parse_result<std::vector<V>>{parse_state<std::vector<V>>{std::move(result), cur}};
    }};
}

/// Returns a parser that tries @p p and wraps the result in @c std::optional.
///
/// Always succeeds: yields @c std::nullopt if @p p fails without consuming
/// input, otherwise yields the value. If @p p fails *having* consumed input
/// the failure is propagated (commit-on-consumed-input applies here too).
///
/// @tparam P Parser to attempt.
template <parser_like P>
[[nodiscard]] constexpr auto opt(P p) {
    return parser{[p](cursor cur) {
        using V    = decltype(p(cur)->value);
        auto start = cur.position().offset;
        auto r     = p(cur);
        if (r.has_value()) {
            return parse_result<std::optional<V>>{parse_state<std::optional<V>>{std::move(r->value), r->rest}};
        }
        if (r.error().where.offset != start) {
            return parse_result<std::optional<V>>{std::unexpected(r.error())};
        }
        return parse_result<std::optional<V>>{parse_state<std::optional<V>>{std::nullopt, cur}};
    }};
}

/// Returns a parser that strips leading and trailing inter-token whitespace
/// around @p p.
///
/// This is the standard way to make a token parser whitespace-insensitive in
/// a recursive-descent setting.
///
/// @tparam P Parser to wrap.
template <parser_like P>
[[nodiscard]] constexpr auto lexeme(P p) {
    return parser{[p](cursor cur) {
        auto start = skip_intertoken_space(cur);
        auto r     = p(start);
        if (!r.has_value())
            return r;
        auto rest = skip_intertoken_space(r->rest);
        using V   = decltype(r->value);
        return parse_result<V>{parse_state<V>{r->value, rest}};
    }};
}

/// Returns a parser for one or more decimal digits, converted to @c int via
/// @c std::from_chars.
///
/// Unlike a throwing @c std::stoi conversion (decision parser-combinators),
/// an out-of-range run of digits is reported as a positioned
/// @ref parse_error rather than an exception — there is no unsigned-integer
/// spelling this parser accepts that can crash the caller.
[[nodiscard]] constexpr auto digits() {
    return parser{[](cursor cur) -> parse_result<int> {
        const auto start = cur;
        while (!cur.empty() && cur.peek() >= '0' && cur.peek() <= '9') { // substrate generic algorithm
            cur = cur.bump();
        }
        const std::size_t count = cur.position().offset - start.position().offset;
        if (count == 0) {
            return std::unexpected(parse_error{start.position(), "expected digits"});
        }
        std::string_view span = start.remaining().substr(0, count);
        int              value{};
        auto [ptr, ec] = std::from_chars(span.data(), span.data() + span.size(), value);
        if (ec == std::errc::result_out_of_range) {
            return std::unexpected(parse_error{start.position(), "digits: value out of range"});
        }
        // span is exactly a run of '0'-'9', so no other std::from_chars error
        // (invalid_argument, partial parse) is reachable here.
        return parse_state<int>{value, cur};
    }};
}

} // namespace beman::specgen::foundation::parse

#endif // BEMAN_SPECGEN_FOUNDATION_PARSE_PARSER_HPP
