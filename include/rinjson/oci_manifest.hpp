/* SPDX-License-Identifier: MIT */
#ifndef RINJSON_OCI_MANIFEST_HPP
#define RINJSON_OCI_MANIFEST_HPP

#include "json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rinjson {

/* The descriptor fields retained by the native registry consumer.  The
 * parser deliberately does not resolve URLs or fetch referenced content. */
struct OciDescriptor {
    std::string mediaType;
    std::string digest;
    std::uint64_t size = 0u;
};

struct OciImageManifest {
    std::uint64_t schemaVersion = 0u;
    std::string mediaType;
    bool hasConfig = false;
    OciDescriptor config;
    std::vector<OciDescriptor> layers;
};

struct OciManifestLimits {
    std::size_t maxBytes = 4u * 1024u * 1024u;
    std::size_t maxDepth = 16u;
    std::size_t maxStringBytes = 4096u;
    std::size_t maxLayers = 128u;
};

/* Parse and semantically validate one OCI/Docker image manifest.  This is a
 * bounded, allocation-owning model adapter: the caller still owns all blob
 * retrieval and content admission decisions. */
[[nodiscard]] bool parseOciImageManifest(std::string_view input,
                                         OciImageManifest& output,
                                         const OciManifestLimits& limits = {});

} // namespace rinjson

#endif
