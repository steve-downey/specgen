// include/beman/specgen/foundation/json_descriptor.hpp            -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Per-type member descriptor tables driving JSON emit AND parse from one
// shared table (decision json-single-schema). Two hand-spelled descriptions
// of one schema -- an emit half and a parse half -- would make every field
// addition a two-place edit with silent drift risk.
// Here a type's shape is declared once, as a `json_descriptor<T>`
// specialization holding a `constexpr` tuple of field descriptors
// (`field`/`enum_field`/`optional_projected_field`) or, for a tagged
// variant, an `alternatives(...)` descriptor. `emit_json_described` and
// `parse_json_described` are the generic walkers that read that same table
// to produce, or consume, the JSON object; round-trip holds by construction
// because both directions walk the identical `constexpr` value.
//
// This header is IR-agnostic: it names no beman::specgen::ir type. ir.cpp
// specializes json_descriptor<T> for each IR type (the payload tables) and
// calls these walkers.
//
// `Reader` is the lexer core: the byte-at-a-time JSON primitives
// (skip_ws/peek/consume/expect/parse_string/parse_uint/object/array/
// skip_value) that the generic engine below builds its per-type walking on.
// It lives here, rather than privately in ir.cpp, because it is JSON-generic
// (no ir:: type appears in its interface) and the generic engine needs to
// call it.
#ifndef BEMAN_SPECGEN_FOUNDATION_JSON_DESCRIPTOR_HPP
#define BEMAN_SPECGEN_FOUNDATION_JSON_DESCRIPTOR_HPP

#include <beman/specgen/foundation/json_writer.hpp>

#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace beman::specgen::foundation {

// --- lexer core -------------------------------------------------------------

/// Recursive-descent reader over a permissive subset of JSON: the shape the
/// json_descriptor-driven emitters below produce, plus hand-edited goldens
/// (extra whitespace, reordered keys, unknown keys). Every parse function
/// returns false on failure and leaves the first error recorded; callers
/// stop at the first false.
///
/// This is a hand-written lexer over a character buffer: a lexer *is* the
/// character-scan primitive, so its loops (skip_ws, parse_string, the \u
/// escape, parse_uint, object, array) are substrate -- there is no smaller
/// unit to fold over. Each loop still carries its own
/// `// substrate generic algorithm` marker below, since a class-level
/// comment does not reach into a member function's body.
class Reader {
  public:
    explicit Reader(std::string_view text) : text_(text) {}

    bool               failed() const { return failed_; }
    std::size_t        offset() const { return error_offset_; }
    const std::string& message() const { return message_; }

    bool fail(std::string msg) {
        if (!failed_) {
            failed_       = true;
            error_offset_ = pos_;
            message_      = std::move(msg);
        }
        return false;
    }

    void skip_ws() {
        // substrate generic algorithm -- whitespace scan; part of the
        // Reader lexer (see the class doc above).
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool at_end() {
        skip_ws();
        return pos_ >= text_.size();
    }

    bool peek(char c) {
        skip_ws();
        return pos_ < text_.size() && text_[pos_] == c;
    }

    bool consume(char c) {
        if (!peek(c))
            return false;
        ++pos_;
        return true;
    }

    bool expect(char c) {
        if (consume(c))
            return true;
        return fail(std::string("expected '") + c + "'");
    }

    bool parse_string(std::string& out) {
        skip_ws();
        if (pos_ >= text_.size() || text_[pos_] != '"')
            return fail("expected string");
        ++pos_;
        out.clear();
        // substrate generic algorithm -- the string-body scan, part of the
        // Reader lexer (see the class doc above).
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size())
                break;
            const char esc = text_[pos_++];
            switch (esc) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                if (pos_ + 4 > text_.size())
                    return fail("truncated \\u escape");
                unsigned cp = 0;
                // substrate generic algorithm -- the four hex digits of a \u
                // escape. What is folded here is not a range: each iteration
                // consumes one character from `pos_`, the lexer's own cursor,
                // and can abandon the whole parse. That is the character-scan
                // primitive the class doc above calls substrate, the same as
                // skip_ws and parse_string, not a fold wearing a costume --
                // there is no smaller unit left to fold over.
                for (int i = 0; i < 4; ++i) {
                    const char h = text_[pos_++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9')
                        cp |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f')
                        cp |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F')
                        cp |= static_cast<unsigned>(h - 'A' + 10);
                    else
                        return fail("bad hex digit in \\u escape");
                }
                // The emitter only produces \u for control characters; encode
                // the BMP code point as UTF-8. Surrogate pairs are not joined,
                // which no emitted document needs.
                if (cp < 0x80) {
                    out += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_uint(std::size_t& out) {
        skip_ws();
        const std::size_t start = pos_;
        std::size_t       value = 0;
        // substrate generic algorithm -- digit accumulation, part of the
        // Reader lexer (see the class doc above).
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            value = value * 10 + static_cast<std::size_t>(text_[pos_] - '0');
            ++pos_;
        }
        if (pos_ == start)
            return fail("expected number");
        out = value;
        return true;
    }

    bool parse_bool(bool& out) {
        skip_ws();
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = true;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = false;
            return true;
        }
        return fail("expected boolean");
    }

    // Calls on_key(key) for each member; on_key parses that member's value.
    template <class OnKey>
    bool object(OnKey on_key) {
        if (!expect('{'))
            return false;
        if (consume('}'))
            return true;
        // substrate generic algorithm -- comma-separated members, part of
        // the Reader lexer (see the class doc above).
        do {
            std::string key;
            if (!parse_string(key))
                return false;
            if (!expect(':'))
                return false;
            if (!on_key(std::string_view(key)))
                return false;
        } while (consume(','));
        return expect('}');
    }

    template <class OnItem>
    bool array(OnItem on_item) {
        if (!expect('['))
            return false;
        if (consume(']'))
            return true;
        // substrate generic algorithm -- comma-separated elements, part of
        // the Reader lexer (see the class doc above).
        do {
            if (!on_item())
                return false;
        } while (consume(','));
        return expect(']');
    }

    // Discard one value of any shape, so unknown keys do not break parsing.
    bool skip_value() {
        skip_ws();
        if (pos_ >= text_.size())
            return fail("unexpected end of input");
        const char c = text_[pos_];
        if (c == '"') {
            std::string ignored;
            return parse_string(ignored);
        }
        if (c == '{')
            return object([this](std::string_view) { return skip_value(); });
        if (c == '[')
            return array([this] { return skip_value(); });
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return true;
        }
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return true;
        }
        std::size_t ignored = 0;
        return parse_uint(ignored);
    }

  private:
    std::string_view text_;
    std::size_t      pos_          = 0;
    bool             failed_       = false;
    std::size_t      error_offset_ = 0;
    std::string      message_;
};

// --- descriptor vocabulary --------------------------------------------------

/// Per-type schema, empty until specialized: a `members` tuple (plain
/// object) or an `alts` value from `alternatives(...)` (tagged variant).
/// Never specialized for string/size_t/std::vector<T>, handled by shape.
template <typename T>
struct json_descriptor {};

/// One JSON key <-> one data member (string / size_t / vector<U> / a nested
/// described type), handled structurally by member type.
template <typename C, typename T>
struct Field {
    std::string_view name;
    T C::* member;
};

template <typename C, typename T>
constexpr Field<C, T> field(std::string_view name, T C::* member) {
    return Field<C, T>{name, member};
}

/// One JSON key <-> one enum member, via an existing element_name-style
/// to-name/from-name pair; `label` names the value in a parse-error message.
template <typename C, typename E>
struct EnumField {
    std::string_view name;
    E C::* member;
    std::string_view (*to_name)(E);
    std::optional<E> (*from_name)(std::string_view);
    std::string_view label;
};

template <typename C, typename E>
constexpr EnumField<C, E> enum_field(std::string_view name,
                                     E C::* member,
                                     std::string_view (*to_name)(E),
                                     std::optional<E> (*from_name)(std::string_view),
                                     std::string_view label) {
    return EnumField<C, E>{name, member, to_name, from_name, label};
}

/// One JSON key <-> an optional value. Present values retain their ordinary
/// JSON shape; an absent value omits the key. Unlike optional_projected_field,
/// this keeps the wrapped object's own member names.
template <typename C, typename T>
struct OptionalField {
    std::string_view name;
    std::optional<T> C::* member;
};

template <typename C, typename T>
constexpr OptionalField<C, T> optional_field(std::string_view name, std::optional<T> C::* member) {
    return OptionalField<C, T>{name, member};
}

/// One JSON key <-> the single field of an `optional<Wrapper>` member,
/// serialized transparently (present -> the wrapped value, no object around
/// it; absent -> key omitted). Covers ir.hpp's Itemize and EquivalentTo,
/// newtypes that exist only to make "present or not" expressible.
template <typename C, typename Wrapper, typename Inner>
struct OptionalProjectedField {
    std::string_view       name;
    std::optional<Wrapper> C::* member;
    Inner Wrapper::* inner;
};

template <typename C, typename Wrapper, typename Inner>
constexpr auto
optional_projected_field(std::string_view name, std::optional<Wrapper> C::* member, Inner Wrapper::* inner) {
    return OptionalProjectedField<C, Wrapper, Inner>{name, member, inner};
}

/// One alternative of a tagged variant: the tag string it serializes under.
template <typename T>
struct Alt {
    using type = T;
    std::string_view tag;
};

template <typename T>
constexpr Alt<T> alt(std::string_view tag) {
    return Alt<T>{tag};
}

/// A tagged-variant descriptor: the tag key, an error-message label, alts.
template <typename... Ts>
struct Alternatives {
    std::string_view       tag_key;
    std::string_view       label;
    std::tuple<Alt<Ts>...> alts;
};

template <typename... Ts>
constexpr Alternatives<Ts...> alternatives(std::string_view tag_key, std::string_view label, Alt<Ts>... alts) {
    return Alternatives<Ts...>{tag_key, label, std::tuple{alts...}};
}

template <typename T>
concept HasAlts = requires { json_descriptor<T>::alts; };

/// The tag @p T serializes under. Carries the same tripwire as the
/// `overloaded` visitor (decision visitation-rules): an alternative added to
/// the variant but not to its `alternatives(...)` table would otherwise pick
/// up a default-constructed (empty) tag and emit `"type": ""` silently. The
/// static_assert makes that omission a compile error naming the case, exactly
/// as a forgotten `operator()` overload is in an `overloaded` visit.
template <typename T, typename... Ts>
constexpr std::string_view tag_for(const std::tuple<Alt<Ts>...>& alts) {
    static_assert((std::is_same_v<T, Ts> || ...),
                  "variant alternative has no alt(...) row in its alternatives(...) table");
    std::string_view tag;
    std::apply(
        [&](const auto&... a) {
            ((std::is_same_v<T, typename std::decay_t<decltype(a)>::type> ? void(tag = a.tag) : void()), ...);
        },
        alts);
    return tag;
}

// --- generic emit ------------------------------------------------------------

template <typename T>
void emit_value(const T& value, std::string& out);

template <typename T>
void emit_value(const std::vector<T>& values, std::string& out) {
    json_array items(out);
    // substrate generic algorithm -- implements array emission itself, the
    // very verb a caller would otherwise write a loop for.
    for (const auto& value : values)
        emit_value(value, items.element());
}

template <typename C, typename T>
void emit_member(const C& value, json_object& obj, const Field<C, T>& f) {
    emit_value(value.*f.member, obj.key(f.name));
}

template <typename C, typename E>
void emit_member(const C& value, json_object& obj, const EnumField<C, E>& f) {
    write_json_string(f.to_name(value.*f.member), obj.key(f.name));
}

template <typename C, typename Wrapper, typename Inner>
void emit_member(const C& value, json_object& obj, const OptionalProjectedField<C, Wrapper, Inner>& f) {
    if (const auto& wrapped = value.*f.member)
        emit_value((*wrapped).*f.inner, obj.key(f.name));
}

template <typename C, typename T>
void emit_member(const C& value, json_object& obj, const OptionalField<C, T>& f) {
    if (const auto& wrapped = value.*f.member)
        emit_value(*wrapped, obj.key(f.name));
}

template <typename T, typename Members>
void emit_members_into(const T& value, json_object& obj, const Members& members) {
    std::apply([&](const auto&... f) { (emit_member(value, obj, f), ...); }, members);
}

/// Writes a JSON object from an explicit member table -- the primitive both
/// json_descriptor<T>::members (below) and the load-bearing meta-test
/// (json_descriptor.test.cpp) use, so there is exactly one place that opens
/// an object and walks a table into it.
template <typename T, typename Members>
void emit_json_described(const T& value, std::string& out, const Members& members) {
    json_object obj(out);
    emit_members_into(value, obj, members);
}

/// Emits a tagged variant: the tag for whichever alternative is active, then
/// that alternative's own members into the same object. One generic dispatch
/// site, not a per-alternative visitor (decision visitation-rules): every case
/// here is handled the identical, table-driven way, so it stays a lambda.
template <typename Variant>
void emit_variant(const Variant& variant, std::string& out) {
    std::visit(
        [&](const auto& alternative) {
            using T          = std::decay_t<decltype(alternative)>;
            const auto& desc = json_descriptor<Variant>::alts;
            json_object obj(out);
            write_json_string(tag_for<T>(desc.alts), obj.key(desc.tag_key));
            emit_members_into(alternative, obj, json_descriptor<T>::members);
        },
        variant);
}

template <typename T>
void emit_value(const T& value, std::string& out) {
    if constexpr (std::is_same_v<T, std::string>) {
        write_json_string(value, out);
    } else if constexpr (std::is_same_v<T, std::size_t>) {
        // std::format rather than stream insertion: the digits are the
        // same, but formatting a frozen on-disk number this way is
        // locale-*independent*, where `os << n` would quietly expose the IR
        // format to whatever locale a caller had imbued
        // (decision format-print-output).
        std::format_to(std::back_inserter(out), "{}", value);
    } else if constexpr (std::is_same_v<T, bool>) {
        out += value ? "true" : "false";
    } else if constexpr (HasAlts<T>) {
        emit_variant(value, out);
    } else {
        emit_json_described(value, out, json_descriptor<T>::members);
    }
}

// --- generic parse -----------------------------------------------------------

template <typename T>
bool parse_value(Reader& r, T& out);

template <typename T>
bool parse_value(Reader& r, std::vector<T>& out) {
    out.clear();
    return r.array([&] {
        T item{};
        if (!parse_value(r, item))
            return false;
        out.push_back(std::move(item));
        return true;
    });
}

template <typename C, typename T>
bool parse_member(Reader& r, C& out, const Field<C, T>& f) {
    return parse_value(r, out.*f.member);
}

template <typename C, typename E>
bool parse_member(Reader& r, C& out, const EnumField<C, E>& f) {
    std::string name;
    if (!r.parse_string(name))
        return false;
    if (auto value = f.from_name(name)) {
        out.*f.member = *value;
        return true;
    }
    return r.fail("unknown " + std::string(f.label) + " '" + name + "'");
}

template <typename C, typename Wrapper, typename Inner>
bool parse_member(Reader& r, C& out, const OptionalProjectedField<C, Wrapper, Inner>& f) {
    Wrapper wrapped{};
    if (!parse_value(r, wrapped.*f.inner))
        return false;
    out.*f.member = std::move(wrapped);
    return true;
}

template <typename C, typename T>
bool parse_member(Reader& r, C& out, const OptionalField<C, T>& f) {
    T value{};
    if (!parse_value(r, value))
        return false;
    out.*f.member = std::move(value);
    return true;
}

/// Tries every field's name against @p key; nullopt means none matched (the
/// caller should skip the value), else the bool is that field's parse result.
template <typename C, typename Members>
std::optional<bool> try_dispatch(Reader& r, C& out, std::string_view key, const Members& members) {
    std::optional<bool> result;
    std::apply(
        [&](const auto&... f) {
            ((!result.has_value() && key == f.name ? void(result = parse_member(r, out, f)) : void()), ...);
        },
        members);
    return result;
}

/// Reads a JSON object into @p out from an explicit member table -- the
/// primitive both json_descriptor<T>::members (below) and the load-bearing
/// meta-test (json_descriptor.test.cpp) use.
template <typename T, typename Members>
bool parse_json_described(Reader& r, T& out, const Members& members) {
    out = T{};
    return r.object([&](std::string_view key) {
        if (auto result = try_dispatch(r, out, key, members))
            return *result;
        return r.skip_value();
    });
}

/// Parses a tagged variant: every alternative's fields are staged into a
/// same-shaped tuple as the object is scanned once; only once the whole
/// object is consumed does the tag string pick which staged alternative
/// moves into @p out -- so the tag key may appear before or after the
/// fields it selects among.
template <typename... Ts>
bool parse_variant(Reader& r, std::variant<Ts...>& out) {
    using Variant          = std::variant<Ts...>;
    const auto&       desc = json_descriptor<Variant>::alts;
    std::string       tag;
    std::tuple<Ts...> staged{};
    const bool        ok = r.object([&](std::string_view key) {
        if (key == desc.tag_key)
            return r.parse_string(tag);
        std::optional<bool> result;
        std::apply(
            [&](auto&... values) {
                ((!result.has_value()
                      ? void(result = try_dispatch(
                                 r, values, key, json_descriptor<std::decay_t<decltype(values)>>::members))
                      : void()),
                 ...);
            },
            staged);
        if (result.has_value())
            return *result;
        return r.skip_value();
    });
    if (!ok)
        return false;
    bool constructed = false;
    std::apply(
        [&](auto&... values) {
            ((!constructed && tag == tag_for<std::decay_t<decltype(values)>>(desc.alts)
                  ? (out = std::move(values), constructed = true)
                  : false),
             ...);
        },
        staged);
    if (!constructed)
        return r.fail("unknown " + std::string(desc.label) + " '" + tag + "'");
    return true;
}

template <typename T>
bool parse_value(Reader& r, T& out) {
    if constexpr (std::is_same_v<T, std::string>) {
        return r.parse_string(out);
    } else if constexpr (std::is_same_v<T, std::size_t>) {
        return r.parse_uint(out);
    } else if constexpr (std::is_same_v<T, bool>) {
        return r.parse_bool(out);
    } else if constexpr (HasAlts<T>) {
        return parse_variant(r, out);
    } else {
        return parse_json_described(r, out, json_descriptor<T>::members);
    }
}

} // namespace beman::specgen::foundation

#endif // BEMAN_SPECGEN_FOUNDATION_JSON_DESCRIPTOR_HPP
