/* SPDX-License-Identifier: MIT */
#include "../include/rinjson/json.hpp"

#include <charconv>
#include <cmath>
#include <limits>

namespace rinjson {
namespace {

template<typename Exception>
[[noreturn]] void raise(const char* message) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
    throw Exception(message);
#else
    (void)message;
    __builtin_trap();
#endif
}

[[noreturn]] void fail(std::string_view input, std::size_t offset,
                       const char* message) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
    throw Error(input, offset, message);
#else
    (void)input;
    (void)offset;
    (void)message;
    __builtin_trap();
#endif
}

bool hexDigit(char c, unsigned& value) {
    if (c >= '0' && c <= '9') value = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') value = static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value = static_cast<unsigned>(c - 'A' + 10);
    else return false;
    return true;
}

void appendUtf8(std::string& output, std::uint32_t point) {
    if (point <= 0x7fu) output.push_back(static_cast<char>(point));
    else if (point <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (point >> 6u)));
        output.push_back(static_cast<char>(0x80u | (point & 0x3fu)));
    } else if (point <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (point >> 12u)));
        output.push_back(static_cast<char>(0x80u | ((point >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (point & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (point >> 18u)));
        output.push_back(static_cast<char>(0x80u | ((point >> 12u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((point >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (point & 0x3fu)));
    }
}

bool validUtf8(std::string_view input) {
    for (std::size_t i = 0u; i < input.size();) {
        const unsigned char first = static_cast<unsigned char>(input[i]);
        std::size_t count = 0u;
        std::uint32_t codepoint = 0u;
        std::uint32_t minimum = 0u;
        if (first <= 0x7fu) { count = 1u; codepoint = first; minimum = 0u; }
        else if (first >= 0xc2u && first <= 0xdfu) {
            count = 2u; codepoint = first & 0x1fu; minimum = 0x80u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            count = 3u; codepoint = first & 0x0fu; minimum = 0x800u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            count = 4u; codepoint = first & 0x07u; minimum = 0x10000u;
        }
        if (count == 0u || i + count > input.size()) return false;
        for (std::size_t j = 1u; j < count; ++j) {
            const unsigned char continuation = static_cast<unsigned char>(input[i + j]);
            if ((continuation & 0xc0u) != 0x80u) return false;
            codepoint = (codepoint << 6u) | (continuation & 0x3fu);
        }
        if (codepoint < minimum || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            return false;
        if (count == 3u && first == 0xe0u &&
            static_cast<unsigned char>(input[i + 1u]) < 0xa0u) return false;
        if (count == 3u && first == 0xedu &&
            static_cast<unsigned char>(input[i + 1u]) >= 0xa0u) return false;
        if (count == 4u && first == 0xf0u &&
            static_cast<unsigned char>(input[i + 1u]) < 0x90u) return false;
        if (count == 4u && first == 0xf4u &&
            static_cast<unsigned char>(input[i + 1u]) > 0x8fu) return false;
        i += count;
    }
    return true;
}

class Parser final {
public:
    Parser(std::string_view input, const Limits& limits)
        : input_(input), limits_(limits) {
        if (input.size() > limits.maxBytes) fail(input_, 0u, "JSON body exceeds limit");
    }

    Value document() {
        skip();
        Value value = valueAt(0u);
        skip();
        if (position_ != input_.size()) fail(input_, position_, "trailing JSON data");
        return value;
    }

private:
    void account(std::size_t bytes) {
        if (bytes > limits_.maxAllocationBytes - allocationBytes_)
            fail(input_, position_, "JSON allocation exceeds limit");
        allocationBytes_ += bytes;
    }

    void node() {
        if (nodes_ >= limits_.maxTotalNodes)
            fail(input_, position_, "JSON node count exceeds limit");
        ++nodes_;
        account(sizeof(Value));
    }

    void skip() {
        while (position_ < input_.size() && (input_[position_] == ' ' ||
               input_[position_] == '\n' || input_[position_] == '\r' ||
               input_[position_] == '\t')) ++position_;
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    Value valueAt(std::size_t depth) {
        if (depth > limits_.maxDepth) fail(input_, position_, "JSON depth exceeds limit");
        skip();
        if (position_ == input_.size()) fail(input_, position_, "unexpected JSON end");
        node();
        switch (input_[position_]) {
        case 'n': return literal("null", Value(nullptr));
        case 't': return literal("true", Value(true));
        case 'f': return literal("false", Value(false));
        case '"': return Value(string());
        case '[': return array(depth + 1u);
        case '{': return object(depth + 1u);
        default:
            if (input_[position_] == '-' ||
                (input_[position_] >= '0' && input_[position_] <= '9')) return number();
            fail(input_, position_, "invalid JSON value");
        }
    }

    Value literal(std::string_view word, Value value) {
        if (input_.substr(position_, word.size()) != word)
            fail(input_, position_, "invalid JSON literal");
        position_ += word.size();
        return value;
    }

    std::uint32_t codeUnit() {
        std::uint32_t result = 0u;
        for (unsigned index = 0u; index < 4u; ++index) {
            if (position_ == input_.size()) fail(input_, position_, "truncated unicode escape");
            unsigned digit = 0u;
            if (!hexDigit(input_[position_++], digit))
                fail(input_, position_ - 1u, "invalid unicode escape");
            result = (result << 4u) | digit;
        }
        return result;
    }

    std::string string() {
        if (!consume('"')) fail(input_, position_, "expected JSON string");
        std::string result;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') {
                if (result.size() > limits_.maxStringBytes || !validUtf8(result))
                    fail(input_, position_, "invalid JSON UTF-8");
                account(result.size());
                return result;
            }
            if (static_cast<unsigned char>(character) < 0x20u)
                fail(input_, position_ - 1u, "control in JSON string");
            if (character != '\\') {
                result.push_back(character);
                if (result.size() > limits_.maxStringBytes)
                    fail(input_, position_, "JSON string exceeds limit");
                continue;
            }
            if (position_ == input_.size()) fail(input_, position_, "truncated JSON escape");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                const std::uint32_t high = codeUnit();
                std::uint32_t point = high;
                if (high >= 0xd800u && high <= 0xdbffu) {
                    if (position_ + 5u > input_.size() || input_[position_] != '\\' ||
                        input_[position_ + 1u] != 'u')
                        fail(input_, position_, "unpaired unicode surrogate");
                    position_ += 2u;
                    const std::uint32_t low = codeUnit();
                    if (low < 0xdc00u || low > 0xdfffu)
                        fail(input_, position_, "invalid unicode surrogate");
                    point = 0x10000u + ((high - 0xd800u) << 10u) + low - 0xdc00u;
                } else if (high >= 0xdc00u && high <= 0xdfffu) {
                    fail(input_, position_, "unpaired unicode surrogate");
                }
                appendUtf8(result, point);
                break;
            }
            default: fail(input_, position_ - 1u, "invalid JSON escape");
            }
            if (result.size() > limits_.maxStringBytes)
                fail(input_, position_, "JSON string exceeds limit");
        }
        fail(input_, position_, "unterminated JSON string");
    }

    Value number() {
        const std::size_t start = position_;
        const bool negative = consume('-');
        if (position_ == input_.size()) fail(input_, position_, "incomplete JSON number");
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') fail(input_, position_, "leading JSON zero");
        } else {
            if (input_[position_] < '1' || input_[position_] > '9')
                fail(input_, position_, "invalid JSON number");
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        bool floating = false;
        if (consume('.')) {
            floating = true;
            if (position_ == input_.size() || input_[position_] < '0' ||
                input_[position_] > '9') fail(input_, position_, "fraction has no digits");
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' ||
                                          input_[position_] == 'E')) {
            floating = true;
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' ||
                                              input_[position_] == '-')) ++position_;
            if (position_ == input_.size() || input_[position_] < '0' ||
                input_[position_] > '9') fail(input_, position_, "exponent has no digits");
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        const std::string_view token = input_.substr(start, position_ - start);
        if (!floating) {
            if (negative) {
                std::int64_t result = 0;
                const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
                    fail(input_, start, "JSON integer out of range");
                return Value(result);
            }
            std::uint64_t result = 0u;
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
                fail(input_, start, "JSON integer out of range");
            return Value(result);
        }
        double result = 0.0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(),
                                            result, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
            !std::isfinite(result)) fail(input_, start, "invalid JSON number");
        return Value(result);
    }

    Value array(std::size_t depth) {
        (void)consume('[');
        Value::Array result;
        skip();
        if (consume(']')) return Value(std::move(result));
        for (;;) {
            if (result.size() >= limits_.maxArrayElements)
                fail(input_, position_, "JSON array exceeds limit");
            result.push_back(valueAt(depth));
            skip();
            if (consume(']')) return Value(std::move(result));
            if (!consume(',')) fail(input_, position_, "expected JSON comma");
        }
    }

    Value object(std::size_t depth) {
        (void)consume('{');
        Value::Object result;
        skip();
        if (consume('}')) return Value(std::move(result));
        for (;;) {
            if (result.size() >= limits_.maxObjectMembers || position_ >= input_.size() ||
                input_[position_] != '"') fail(input_, position_, "invalid JSON object member");
            const std::string key = string();
            skip();
            if (!consume(':')) fail(input_, position_, "expected JSON colon");
            Value value = valueAt(depth);
            auto existing = result.find(key);
            if (existing != result.end()) {
                if (limits_.duplicateKeys == DuplicateKeyPolicy::Reject)
                    fail(input_, position_, "duplicate JSON object member");
                if (limits_.duplicateKeys == DuplicateKeyPolicy::LastWins)
                    existing->second = std::move(value);
            } else {
                account(key.size() + sizeof(Value));
                result.emplace(key, std::move(value));
            }
            skip();
            if (consume('}')) return Value(std::move(result));
            if (!consume(',')) fail(input_, position_, "expected JSON comma");
            skip();
        }
    }

    std::string_view input_;
    const Limits& limits_;
    std::size_t position_ = 0u;
    std::size_t nodes_ = 0u;
    std::size_t allocationBytes_ = 0u;
};

class Writer final {
public:
    explicit Writer(const DumpOptions& options) : options_(options) {}

    std::string finish() { return std::move(output_); }

    void text(std::string_view value) {
        reserve(value.size());
        output_.append(value.data(), value.size());
    }

    void character(char value) {
        reserve(1u);
        output_.push_back(value);
    }

    void indent(std::size_t depth) {
        if (depth != 0u && options_.indentWidth > std::numeric_limits<std::size_t>::max() / depth)
            raise<std::runtime_error>("JSON indentation overflow");
        const std::size_t count = depth * options_.indentWidth;
        reserve(count);
        output_.append(count, ' ');
    }

    void string(std::string_view value) {
        if (!validUtf8(value)) raise<std::runtime_error>("invalid JSON UTF-8 value");
        static constexpr char hex[] = "0123456789abcdef";
        character('"');
        for (const unsigned char characterValue : value) {
            switch (characterValue) {
            case '"': text("\\\""); break;
            case '\\': text("\\\\"); break;
            case '\b': text("\\b"); break;
            case '\f': text("\\f"); break;
            case '\n': text("\\n"); break;
            case '\r': text("\\r"); break;
            case '\t': text("\\t"); break;
            default:
                if (characterValue < 0x20u) {
                    text("\\u00");
                    character(hex[characterValue >> 4u]);
                    character(hex[characterValue & 0x0fu]);
                } else character(static_cast<char>(characterValue));
            }
        }
        character('"');
    }

    void value(const Value& itemValue, std::size_t depth) {
        if (itemValue.isNull()) text("null");
        else if (itemValue.isBool()) text(itemValue.asBool() ? "true" : "false");
        else if (itemValue.isSignedInteger()) number(itemValue.asInteger());
        else if (itemValue.isUnsignedInteger()) number(itemValue.asUnsignedInteger());
        else if (itemValue.isNumber()) number(itemValue.asNumber());
        else if (itemValue.isString()) string(itemValue.asString());
        else if (itemValue.isArray()) {
            character('[');
            bool first = true;
            for (const Value& item : itemValue.asArray()) {
                if (!first) character(',');
                first = false;
                if (options_.pretty) { character('\n'); indent(depth + 1u); }
                value(item, depth + 1u);
            }
            if (options_.pretty && !itemValue.asArray().empty()) { character('\n'); indent(depth); }
            character(']');
        } else {
            character('{');
            bool first = true;
            for (const auto& item : itemValue.asObject()) {
                if (!first) character(',');
                first = false;
                if (options_.pretty) { character('\n'); indent(depth + 1u); }
                string(item.first);
                character(':');
                if (options_.pretty) character(' ');
                value(item.second, depth + 1u);
            }
            if (options_.pretty && !itemValue.asObject().empty()) { character('\n'); indent(depth); }
            character('}');
        }
    }

private:
    void reserve(std::size_t additional) {
        if (additional > options_.maxOutputBytes - output_.size())
            raise<std::runtime_error>("JSON serialized output exceeds limit");
    }

    void number(std::int64_t value) {
        char buffer[32]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{}) raise<std::runtime_error>("JSON integer serialization failed");
        text(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
    }

    void number(std::uint64_t value) {
        char buffer[32]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{}) raise<std::runtime_error>("JSON integer serialization failed");
        text(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
    }

    void number(double value) {
        char buffer[64]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::general);
        if (result.ec != std::errc{} || !std::isfinite(value))
            raise<std::runtime_error>("JSON number serialization failed");
        text(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
    }

    const DumpOptions& options_;
    std::string output_;
};

class StreamParser final {
public:
    StreamParser(std::string_view input, const StreamCallbacks& callbacks,
                 const Limits& limits)
        : input_(input), callbacks_(callbacks), limits_(limits) {
        if (input.size() > limits.maxBytes) fail(input_, 0u, "JSON body exceeds limit");
    }

    void document() {
        skip();
        value(0u);
        skip();
        if (position_ != input_.size()) fail(input_, position_, "trailing JSON data");
    }

private:
    void skip() {
        while (position_ < input_.size() && (input_[position_] == ' ' ||
               input_[position_] == '\n' || input_[position_] == '\r' ||
               input_[position_] == '\t')) ++position_;
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void node() {
        if (nodes_ >= limits_.maxTotalNodes)
            fail(input_, position_, "JSON node count exceeds limit");
        ++nodes_;
    }

    void event(bool (*callback)(void*), const char* message) {
        if (callback != nullptr && !callback(callbacks_.context))
            fail(input_, position_, message);
    }

    void boolean(bool value) {
        if (callbacks_.onBoolean != nullptr &&
            !callbacks_.onBoolean(callbacks_.context, value))
            fail(input_, position_, "JSON stream callback rejected boolean");
    }

    void numberEvent(std::string_view token) {
        if (callbacks_.onNumber != nullptr &&
            !callbacks_.onNumber(callbacks_.context, token))
            fail(input_, position_, "JSON stream callback rejected number");
    }

    static std::size_t utf8Bytes(std::string_view input, std::size_t position) {
        const unsigned char first = static_cast<unsigned char>(input[position]);
        std::size_t count = 0u;
        std::uint32_t codepoint = 0u;
        std::uint32_t minimum = 0u;
        if (first <= 0x7fu) { count = 1u; codepoint = first; minimum = 0u; }
        else if (first >= 0xc2u && first <= 0xdfu) {
            count = 2u; codepoint = first & 0x1fu; minimum = 0x80u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            count = 3u; codepoint = first & 0x0fu; minimum = 0x800u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            count = 4u; codepoint = first & 0x07u; minimum = 0x10000u;
        } else return 0u;
        if (count > input.size() - position) return 0u;
        for (std::size_t index = 1u; index < count; ++index) {
            const unsigned char continuation =
                static_cast<unsigned char>(input[position + index]);
            if ((continuation & 0xc0u) != 0x80u) return 0u;
            codepoint = (codepoint << 6u) | (continuation & 0x3fu);
        }
        if (codepoint < minimum || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return 0u;
        if (count == 3u && first == 0xe0u &&
            static_cast<unsigned char>(input[position + 1u]) < 0xa0u) return 0u;
        if (count == 3u && first == 0xedu &&
            static_cast<unsigned char>(input[position + 1u]) >= 0xa0u) return 0u;
        if (count == 4u && first == 0xf0u &&
            static_cast<unsigned char>(input[position + 1u]) < 0x90u) return 0u;
        if (count == 4u && first == 0xf4u &&
            static_cast<unsigned char>(input[position + 1u]) > 0x8fu) return 0u;
        return count;
    }

    std::uint32_t codeUnit() {
        std::uint32_t result = 0u;
        for (unsigned index = 0u; index < 4u; ++index) {
            if (position_ == input_.size()) fail(input_, position_, "truncated unicode escape");
            unsigned digit = 0u;
            if (!hexDigit(input_[position_++], digit))
                fail(input_, position_ - 1u, "invalid unicode escape");
            result = (result << 4u) | digit;
        }
        return result;
    }

    void string(bool isKey) {
        if (!consume('"')) fail(input_, position_, "expected JSON string");
        const std::size_t tokenStart = position_;
        std::size_t decodedBytes = 0u;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                if (callbacks_.onString != nullptr &&
                    !callbacks_.onString(callbacks_.context,
                        input_.substr(tokenStart, position_ - tokenStart - 1u), isKey))
                    fail(input_, position_, "JSON stream callback rejected string");
                return;
            }
            if (character < 0x20u) fail(input_, position_ - 1u, "control in JSON string");
            if (character != '\\') {
                const std::size_t bytes = utf8Bytes(input_, position_ - 1u);
                if (bytes == 0u) fail(input_, position_ - 1u, "invalid JSON UTF-8");
                position_ += bytes - 1u;
                decodedBytes += bytes;
            } else {
                if (position_ == input_.size()) fail(input_, position_, "truncated JSON escape");
                const char escaped = input_[position_++];
                if (escaped == 'u') {
                    const std::uint32_t high = codeUnit();
                    std::uint32_t point = high;
                    if (high >= 0xd800u && high <= 0xdbffu) {
                        if (position_ + 5u > input_.size() || input_[position_] != '\\' ||
                            input_[position_ + 1u] != 'u')
                            fail(input_, position_, "unpaired unicode surrogate");
                        position_ += 2u;
                        const std::uint32_t low = codeUnit();
                        if (low < 0xdc00u || low > 0xdfffu)
                            fail(input_, position_, "invalid unicode surrogate");
                        point = 0x10000u + ((high - 0xd800u) << 10u) + low - 0xdc00u;
                    } else if (high >= 0xdc00u && high <= 0xdfffu) {
                        fail(input_, position_, "unpaired unicode surrogate");
                    }
                    decodedBytes += point <= 0x7fu ? 1u : point <= 0x7ffu ? 2u :
                                    point <= 0xffffu ? 3u : 4u;
                } else if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                           escaped != 'b' && escaped != 'f' && escaped != 'n' &&
                           escaped != 'r' && escaped != 't') {
                    fail(input_, position_ - 1u, "invalid JSON escape");
                } else {
                    ++decodedBytes;
                }
            }
            if (decodedBytes > limits_.maxStringBytes)
                fail(input_, position_, "JSON string exceeds limit");
        }
        fail(input_, position_, "unterminated JSON string");
    }

    void literal(std::string_view word, bool value) {
        if (input_.substr(position_, word.size()) != word)
            fail(input_, position_, "invalid JSON literal");
        position_ += word.size();
        if (word == "null") event(callbacks_.onNull, "JSON stream callback rejected null");
        else boolean(value);
    }

    void number() {
        const std::size_t start = position_;
        const bool negative = consume('-');
        if (position_ == input_.size()) fail(input_, position_, "incomplete JSON number");
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') fail(input_, position_, "leading JSON zero");
        } else {
            if (input_[position_] < '1' || input_[position_] > '9')
                fail(input_, position_, "invalid JSON number");
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        bool floating = false;
        if (consume('.')) {
            floating = true;
            if (position_ == input_.size() || input_[position_] < '0' ||
                input_[position_] > '9') fail(input_, position_, "fraction has no digits");
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' ||
                                          input_[position_] == 'E')) {
            floating = true;
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' ||
                                              input_[position_] == '-')) ++position_;
            if (position_ == input_.size() || input_[position_] < '0' ||
                input_[position_] > '9') fail(input_, position_, "exponent has no digits");
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        const std::string_view token = input_.substr(start, position_ - start);
        if (!floating) {
            if (negative) {
                std::int64_t result = 0;
                const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
                    fail(input_, start, "JSON integer out of range");
            } else {
                std::uint64_t result = 0u;
                const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
                    fail(input_, start, "JSON integer out of range");
            }
        } else {
            double result = 0.0;
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(),
                                                result, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
                !std::isfinite(result)) fail(input_, start, "invalid JSON number");
        }
        numberEvent(token);
    }

    void value(std::size_t depth) {
        if (depth > limits_.maxDepth) fail(input_, position_, "JSON depth exceeds limit");
        skip();
        if (position_ == input_.size()) fail(input_, position_, "unexpected JSON end");
        node();
        switch (input_[position_]) {
        case 'n': literal("null", false); break;
        case 't': literal("true", true); break;
        case 'f': literal("false", false); break;
        case '"': string(false); break;
        case '[': array(depth + 1u); break;
        case '{': object(depth + 1u); break;
        default:
            if (input_[position_] == '-' || (input_[position_] >= '0' &&
                                             input_[position_] <= '9')) number();
            else fail(input_, position_, "invalid JSON value");
        }
    }

    void array(std::size_t depth) {
        (void)consume('[');
        event(callbacks_.onArrayStart, "JSON stream callback rejected array start");
        skip();
        if (consume(']')) {
            event(callbacks_.onArrayEnd, "JSON stream callback rejected array end");
            return;
        }
        std::size_t count = 0u;
        for (;;) {
            if (count++ >= limits_.maxArrayElements)
                fail(input_, position_, "JSON array exceeds limit");
            value(depth);
            skip();
            if (consume(']')) {
                event(callbacks_.onArrayEnd, "JSON stream callback rejected array end");
                return;
            }
            if (!consume(',')) fail(input_, position_, "expected JSON comma");
            skip();
        }
    }

    void object(std::size_t depth) {
        (void)consume('{');
        event(callbacks_.onObjectStart, "JSON stream callback rejected object start");
        skip();
        if (consume('}')) {
            event(callbacks_.onObjectEnd, "JSON stream callback rejected object end");
            return;
        }
        std::size_t count = 0u;
        for (;;) {
            if (count++ >= limits_.maxObjectMembers || position_ >= input_.size() ||
                input_[position_] != '"')
                fail(input_, position_, "invalid JSON object member");
            string(true);
            skip();
            if (!consume(':')) fail(input_, position_, "expected JSON colon");
            value(depth);
            skip();
            if (consume('}')) {
                event(callbacks_.onObjectEnd, "JSON stream callback rejected object end");
                return;
            }
            if (!consume(',')) fail(input_, position_, "expected JSON comma");
            skip();
        }
    }

    std::string_view input_;
    const StreamCallbacks& callbacks_;
    const Limits& limits_;
    std::size_t position_ = 0u;
    std::size_t nodes_ = 0u;
};

} // namespace

Error::Error(std::string_view input, std::size_t offset, std::string message)
    : std::runtime_error(std::move(message)), offset_(offset), line_(1u), column_(1u) {
    if (offset_ > input.size()) offset_ = input.size();
    for (std::size_t index = 0u; index < offset_; ++index) {
        if (input[index] == '\n') { ++line_; column_ = 1u; }
        else ++column_;
    }
}

bool Value::isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(value_); }
bool Value::isBool() const noexcept { return std::holds_alternative<bool>(value_); }
bool Value::isSignedInteger() const noexcept { return std::holds_alternative<SignedInteger>(value_); }
bool Value::isUnsignedInteger() const noexcept { return std::holds_alternative<UnsignedInteger>(value_); }
bool Value::isInteger() const noexcept { return isSignedInteger() || isUnsignedInteger(); }
bool Value::isNumber() const noexcept { return isInteger() || std::holds_alternative<double>(value_); }
bool Value::isString() const noexcept { return std::holds_alternative<std::string>(value_); }
bool Value::isArray() const noexcept { return std::holds_alternative<Array>(value_); }
bool Value::isObject() const noexcept { return std::holds_alternative<Object>(value_); }
bool Value::asBool() const { return std::get<bool>(value_); }
Value::SignedInteger Value::asInteger() const {
    if (isSignedInteger()) return std::get<SignedInteger>(value_);
    const auto value = std::get<UnsignedInteger>(value_);
    if (value > static_cast<UnsignedInteger>(std::numeric_limits<SignedInteger>::max()))
        raise<std::out_of_range>("JSON unsigned integer does not fit signed integer");
    return static_cast<SignedInteger>(value);
}
Value::UnsignedInteger Value::asUnsignedInteger() const {
    if (isUnsignedInteger()) return std::get<UnsignedInteger>(value_);
    const auto value = std::get<SignedInteger>(value_);
    if (value < 0) raise<std::out_of_range>("JSON signed integer is negative");
    return static_cast<UnsignedInteger>(value);
}
double Value::asNumber() const {
    if (isSignedInteger()) return static_cast<double>(asInteger());
    if (isUnsignedInteger()) return static_cast<double>(asUnsignedInteger());
    return std::get<double>(value_);
}
const std::string& Value::asString() const { return std::get<std::string>(value_); }
const Value::Array& Value::asArray() const { return std::get<Array>(value_); }
Value::Array& Value::asArray() { return std::get<Array>(value_); }
const Value::Object& Value::asObject() const { return std::get<Object>(value_); }
Value::Object& Value::asObject() { return std::get<Object>(value_); }
const Value* Value::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    const auto iterator = asObject().find(key);
    return iterator == asObject().end() ? nullptr : &iterator->second;
}
Value* Value::find(std::string_view key) {
    if (!isObject()) return nullptr;
    auto iterator = asObject().find(key);
    return iterator == asObject().end() ? nullptr : &iterator->second;
}
Value& Value::operator[](std::string key) { return asObject()[std::move(key)]; }
std::string Value::dump() const { return dump(DumpOptions{}); }
std::string Value::dump(const DumpOptions& options) const {
    if (options.maxOutputBytes == 0u) raise<std::invalid_argument>("JSON output limit is zero");
    Writer writer(options);
    writer.value(*this, 0u);
    return writer.finish();
}
Value parse(std::string_view input, const Limits& limits) {
    if (limits.maxBytes == 0u || limits.maxDepth == 0u || limits.maxStringBytes == 0u ||
        limits.maxArrayElements == 0u || limits.maxObjectMembers == 0u ||
        limits.maxTotalNodes == 0u || limits.maxAllocationBytes == 0u)
        fail(input, 0u, "JSON limit is zero");
    return Parser(input, limits).document();
}

bool parseStream(std::string_view input, const StreamCallbacks& callbacks,
                 const Limits& limits) {
    if (limits.maxBytes == 0u || limits.maxDepth == 0u || limits.maxStringBytes == 0u ||
        limits.maxArrayElements == 0u || limits.maxObjectMembers == 0u ||
        limits.maxTotalNodes == 0u || limits.maxAllocationBytes == 0u)
        fail(input, 0u, "JSON limit is zero");
    StreamParser(input, callbacks, limits).document();
    return true;
}

} // namespace rinjson
