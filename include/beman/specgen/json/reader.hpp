// include/beman/specgen/json/reader.hpp                           -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The JSON lexer: the byte-at-a-time reading primitives (skip_ws, peek,
// consume, expect, parse_string, parse_uint, parse_bool, object, array,
// skip_value) that descriptor.hpp's generic parse walks a type's schema with.
// The mirror of writer.hpp: JSON-generic, with no IR type in its interface.
#ifndef BEMAN_SPECGEN_JSON_READER_HPP
#define BEMAN_SPECGEN_JSON_READER_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace beman::specgen::json {

/// Recursive-descent reader over a permissive subset of JSON: the shape the
/// descriptor-driven emitters (descriptor.hpp) produce, plus hand-edited goldens
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

} // namespace beman::specgen::json

#endif // BEMAN_SPECGEN_JSON_READER_HPP
