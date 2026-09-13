/* SPDX-License-Identifier: MIT */
#ifndef RINJSON_JSON_HPP
#define RINJSON_JSON_HPP

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rinjson {

enum class DuplicateKeyPolicy : std::uint8_t {
    Reject,
    FirstWins,
    LastWins,
};

struct Limits {
    std::size_t maxBytes = 4u * 1024u * 1024u;
    std::size_t maxDepth = 32u;
    std::size_t maxStringBytes = 512u * 1024u;
    std::size_t maxArrayElements = 4096u;
    std::size_t maxObjectMembers = 1024u;
    std::size_t maxTotalNodes = 131072u;
    std::size_t maxAllocationBytes = 16u * 1024u * 1024u;
    DuplicateKeyPolicy duplicateKeys = DuplicateKeyPolicy::Reject;
};

class Error final : public std::runtime_error {
public:
    Error(std::string_view input, std::size_t offset, std::string message);
    Error(std::size_t offset, const std::string& message)
        : Error({}, offset, message) {}

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return column_; }

private:
    std::size_t offset_;
    std::size_t line_;
    std::size_t column_;
};

struct DumpOptions {
    bool pretty = false;
    std::size_t indentWidth = 2u;
    std::size_t maxOutputBytes = 16u * 1024u * 1024u;
};

/* Allocation-free syntax/event interface for consumers that do not need a
 * DOM. onString receives the lexical contents between quotes, including JSON
 * escape sequences; isKey distinguishes object member names from values. */
struct StreamCallbacks {
    void* context = nullptr;
    bool (*onNull)(void*) = nullptr;
    bool (*onBoolean)(void*, bool) = nullptr;
    bool (*onNumber)(void*, std::string_view) = nullptr;
    bool (*onString)(void*, std::string_view, bool isKey) = nullptr;
    bool (*onArrayStart)(void*) = nullptr;
    bool (*onArrayEnd)(void*) = nullptr;
    bool (*onObjectStart)(void*) = nullptr;
    bool (*onObjectEnd)(void*) = nullptr;
};

class Value {
public:
    using SignedInteger = std::int64_t;
    using UnsignedInteger = std::uint64_t;
    using Array = std::vector<Value>;
    /* std::map gives every object a canonical lexicographic key order. */
    using Object = std::map<std::string, Value, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, SignedInteger,
                                 UnsignedInteger, double, std::string, Array,
                                 Object>;

    Value() : value_(nullptr) {}
    Value(std::nullptr_t) : value_(nullptr) {}
    Value(bool value) : value_(value) {}
    Value(SignedInteger value) : value_(value) {}
    Value(UnsignedInteger value) : value_(value) {}
    Value(int value) : value_(static_cast<SignedInteger>(value)) {}
    Value(unsigned int value) : value_(static_cast<UnsignedInteger>(value)) {}
    Value(double value) : value_(value) {}
    Value(const char* value) : value_(std::string(value == nullptr ? "" : value)) {}
    Value(std::string value) : value_(std::move(value)) {}
    Value(Array value) : value_(std::move(value)) {}
    Value(Object value) : value_(std::move(value)) {}

    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBool() const noexcept;
    [[nodiscard]] bool isSignedInteger() const noexcept;
    [[nodiscard]] bool isUnsignedInteger() const noexcept;
    [[nodiscard]] bool isInteger() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;
    [[nodiscard]] bool asBool() const;
    [[nodiscard]] SignedInteger asInteger() const;
    [[nodiscard]] UnsignedInteger asUnsignedInteger() const;
    [[nodiscard]] double asNumber() const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array& asArray() const;
    [[nodiscard]] Array& asArray();
    [[nodiscard]] const Object& asObject() const;
    [[nodiscard]] Object& asObject();
    [[nodiscard]] const Value* find(std::string_view key) const;
    [[nodiscard]] Value* find(std::string_view key);
    Value& operator[](std::string key);
    [[nodiscard]] std::string dump() const;
    [[nodiscard]] std::string dump(const DumpOptions& options) const;

private:
    Storage value_;
};

[[nodiscard]] Value parse(std::string_view input, const Limits& limits = Limits{});
/* Validate and emit one complete JSON document without constructing a DOM.
 * Callbacks are invoked in document order and may reject the parse by
 * returning false. Duplicate keys are delivered in input order; callers that
 * need Reject/FirstWins/LastWins semantics must apply that policy in the
 * bounded consumer callback or use parse(). */
bool parseStream(std::string_view input, const StreamCallbacks& callbacks,
                 const Limits& limits = Limits{});

} // namespace rinjson

#endif
