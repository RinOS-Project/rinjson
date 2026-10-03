/* SPDX-License-Identifier: MIT */
#include "../include/rinjson/oci_manifest.hpp"

#include <cctype>
#include <limits>
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#    include <new>
#endif

namespace rinjson {
namespace {

const Value* field(const Value& object, std::string_view name) {
    return object.isObject() ? object.find(name) : nullptr;
}

bool unsignedValue(const Value* value, std::uint64_t& output) {
    if (value == nullptr || !value->isInteger()) return false;
    if (value->isUnsignedInteger()) {
        output = value->asUnsignedInteger();
        return true;
    }
    const std::int64_t signedValue = value->asInteger();
    if (signedValue < 0) return false;
    output = static_cast<std::uint64_t>(signedValue);
    return true;
}

bool safeString(const Value* value, std::string& output,
                std::size_t maximum, bool required) {
    if (value == nullptr || !value->isString()) return false;
    const std::string& source = value->asString();
    if ((required && source.empty()) || source.size() > maximum) return false;
    for (const unsigned char character : source) {
        if (character < 0x20u || character == 0x7fu) return false;
    }
    output = source;
    return true;
}

bool digestSyntax(const std::string& value) {
    const std::size_t separator = value.find(':');
    if (separator == std::string::npos || separator == 0u ||
        separator + 1u >= value.size())
        return false;

    const auto algorithmComponent = [](std::string_view component) {
        if (component.empty()) return false;
        for (const unsigned char character : component) {
            if (!((character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9')))
                return false;
        }
        return true;
    };
    std::size_t componentStart = 0u;
    for (std::size_t index = 0u; index <= separator; ++index) {
        if (index != separator && value[index] != '+' && value[index] != '.' &&
            value[index] != '_' && value[index] != '-')
            continue;
        if (!algorithmComponent(std::string_view(value).substr(
                componentStart, index - componentStart)))
            return false;
        componentStart = index + 1u;
    }

    const std::string_view encoded(value.data() + separator + 1u,
                                   value.size() - separator - 1u);
    for (const unsigned char character : encoded) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '=' ||
              character == '_' || character == '-'))
            return false;
    }

    const std::string_view algorithm(value.data(), separator);
    if (algorithm == "sha256" || algorithm == "blake3") {
        if (encoded.size() != 64u) return false;
        for (const unsigned char character : encoded)
            if (!((character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f')))
                return false;
    } else if (algorithm == "sha512") {
        if (encoded.size() != 128u) return false;
        for (const unsigned char character : encoded)
            if (!((character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f')))
                return false;
    }
    return true;
}

bool descriptor(const Value& value, OciDescriptor& output,
                const OciManifestLimits& limits) {
    if (!value.isObject()) return false;
    OciDescriptor parsed;
    if (!safeString(field(value, "mediaType"), parsed.mediaType,
                    limits.maxStringBytes, true) ||
        !safeString(field(value, "digest"), parsed.digest,
                    limits.maxStringBytes, true) ||
        !digestSyntax(parsed.digest) ||
        !unsignedValue(field(value, "size"), parsed.size))
        return false;
    output = std::move(parsed);
    return true;
}

} // namespace

bool parseOciImageManifest(std::string_view input, OciImageManifest& output,
                           const OciManifestLimits& limits) {
    if (input.empty() || limits.maxBytes == 0u || input.size() > limits.maxBytes ||
        limits.maxDepth == 0u || limits.maxStringBytes == 0u ||
        limits.maxLayers == 0u)
        return false;

    Limits parserLimits;
    parserLimits.maxBytes = limits.maxBytes;
    parserLimits.maxDepth = limits.maxDepth;
    parserLimits.maxStringBytes = limits.maxStringBytes;
    parserLimits.maxArrayElements = limits.maxLayers;
    parserLimits.maxObjectMembers = 32u;
    parserLimits.maxTotalNodes = 4096u;
    parserLimits.maxAllocationBytes = limits.maxBytes >
            (std::numeric_limits<std::size_t>::max() / 4u)
        ? std::numeric_limits<std::size_t>::max()
        : limits.maxBytes * 4u;

    try {
        const Value root = parse(input, parserLimits);
        if (!root.isObject()) return false;

        OciImageManifest parsed;
        if (!unsignedValue(field(root, "schemaVersion"), parsed.schemaVersion) ||
            parsed.schemaVersion != 2u)
            return false;

        if (const Value* mediaType = field(root, "mediaType");
            mediaType != nullptr &&
            !safeString(mediaType, parsed.mediaType, limits.maxStringBytes, true))
            return false;

        if (const Value* config = field(root, "config"); config != nullptr) {
            if (!descriptor(*config, parsed.config, limits)) return false;
            parsed.hasConfig = true;
        }

        const Value* layers = field(root, "layers");
        if (layers == nullptr || !layers->isArray() || layers->asArray().empty() ||
            layers->asArray().size() > limits.maxLayers)
            return false;
        parsed.layers.reserve(layers->asArray().size());
        for (const Value& layer : layers->asArray()) {
            OciDescriptor parsedLayer;
            if (!descriptor(layer, parsedLayer, limits)) return false;
            parsed.layers.push_back(std::move(parsedLayer));
        }

        output = std::move(parsed);
        return true;
    } catch (const Error&) {
        return false;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    } catch (const std::bad_alloc&) {
        return false;
#endif
    }
}

} // namespace rinjson
