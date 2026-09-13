/* SPDX-License-Identifier: MIT */
#include "../include/rinjson/oci_manifest.hpp"

#include <cctype>
#include <limits>

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

bool sha256Digest(const std::string& value) {
    if (value.size() != 71u || value.compare(0u, 7u, "sha256:") != 0)
        return false;
    for (std::size_t index = 7u; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (!std::isxdigit(character)) return false;
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
        !sha256Digest(parsed.digest) ||
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
    }
}

} // namespace rinjson
