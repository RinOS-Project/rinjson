/* SPDX-License-Identifier: MIT */
/* Allocation-free strict JSON reader for freestanding C consumers. */

#ifndef RINJSON_C_READER_H
#define RINJSON_C_READER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RinJsonCStatus {
    RIN_JSON_C_OK = 0,
    RIN_JSON_C_INVALID_ARGUMENT = -1,
    RIN_JSON_C_MALFORMED = -2,
    RIN_JSON_C_CAPACITY = -3
} RinJsonCStatus;

typedef struct RinJsonCSpan {
    const char* data;
    uint32_t size;
} RinJsonCSpan;

typedef struct RinJsonCReader {
    const char* bytes;
    uint32_t length;
    uint32_t offset;
    uint32_t max_depth;
    uint32_t max_tokens;
    uint32_t max_string_bytes;
    uint32_t tokens;
} RinJsonCReader;

/* The input is borrowed for the lifetime of the reader. Limits are mandatory
 * so callers cannot accidentally turn a protocol parser into an unbounded
 * scan. The reader itself allocates no storage. */
RinJsonCStatus rin_json_c_reader_init(
    RinJsonCReader* reader, const char* bytes, uint32_t length,
    uint32_t max_bytes, uint32_t max_depth, uint32_t max_tokens,
    uint32_t max_string_bytes);

void rin_json_c_skip_space(RinJsonCReader* reader);
RinJsonCStatus rin_json_c_consume(RinJsonCReader* reader, char expected);

/* Return the lexical contents between quotes. JSON escapes remain encoded in
 * the returned span, but the complete string and UTF-8/Unicode escape form is
 * validated. On failure span_out is cleared. */
RinJsonCStatus rin_json_c_read_string_span(
    RinJsonCReader* reader, RinJsonCSpan* span_out);

/* Read a canonical printable-ASCII string into caller-owned storage. This is
 * intentionally stricter than JSON: escapes, controls, DEL, and non-ASCII
 * bytes are rejected instead of being decoded into an identity field. */
RinJsonCStatus rin_json_c_read_ascii_string(
    RinJsonCReader* reader, char* output, uint32_t capacity,
    uint32_t* bytes_out);

/* Read an unsigned JSON integer with no sign, fraction, exponent, or leading
 * zero. The delimiter remains for the caller's structural parser. On failure
 * value_out is set to zero. */
RinJsonCStatus rin_json_c_read_u32(
    RinJsonCReader* reader, uint32_t* value_out);

/* Validate and consume one complete JSON value, returning its borrowed raw
 * span. Objects and arrays are recursively checked under max_depth and all
 * values/keys count against max_tokens. On failure span_out is cleared. */
RinJsonCStatus rin_json_c_skip_value(
    RinJsonCReader* reader, RinJsonCSpan* span_out);

RinJsonCStatus rin_json_c_document_complete(RinJsonCReader* reader);

#ifdef __cplusplus
}
#endif

#endif /* RINJSON_C_READER_H */
