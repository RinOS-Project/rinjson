/* SPDX-License-Identifier: MIT */

#include "../include/rinjson/c_reader.h"

#include <stddef.h>

static int json_is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int json_hex(char value, uint32_t* digit_out)
{
    if (value >= '0' && value <= '9') *digit_out = (uint32_t)(value - '0');
    else if (value >= 'a' && value <= 'f')
        *digit_out = (uint32_t)(value - 'a' + 10);
    else if (value >= 'A' && value <= 'F')
        *digit_out = (uint32_t)(value - 'A' + 10);
    else return 0;
    return 1;
}

static RinJsonCStatus json_token(RinJsonCReader* reader)
{
    if (reader == NULL || reader->tokens >= reader->max_tokens)
        return RIN_JSON_C_CAPACITY;
    ++reader->tokens;
    return RIN_JSON_C_OK;
}

static RinJsonCStatus json_utf8_bytes(const RinJsonCReader* reader,
                                      uint32_t offset, uint32_t* bytes_out)
{
    const unsigned char first = (unsigned char)reader->bytes[offset];
    uint32_t count;
    uint32_t codepoint;
    uint32_t minimum;
    uint32_t index;

    if (first <= 0x7fu) {
        *bytes_out = 1u;
        return RIN_JSON_C_OK;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        count = 2u;
        codepoint = first & 0x1fu;
        minimum = 0x80u;
    } else if (first >= 0xe0u && first <= 0xefu) {
        count = 3u;
        codepoint = first & 0x0fu;
        minimum = 0x800u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        count = 4u;
        codepoint = first & 0x07u;
        minimum = 0x10000u;
    } else {
        return RIN_JSON_C_MALFORMED;
    }
    if (count > reader->length - offset) return RIN_JSON_C_MALFORMED;
    for (index = 1u; index < count; ++index) {
        const unsigned char continuation =
            (unsigned char)reader->bytes[offset + index];
        if ((continuation & 0xc0u) != 0x80u) return RIN_JSON_C_MALFORMED;
        codepoint = (codepoint << 6u) | (continuation & 0x3fu);
    }
    if (codepoint < minimum || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu))
        return RIN_JSON_C_MALFORMED;
    if (count == 3u && first == 0xe0u &&
        (unsigned char)reader->bytes[offset + 1u] < 0xa0u)
        return RIN_JSON_C_MALFORMED;
    if (count == 3u && first == 0xedu &&
        (unsigned char)reader->bytes[offset + 1u] >= 0xa0u)
        return RIN_JSON_C_MALFORMED;
    if (count == 4u && first == 0xf0u &&
        (unsigned char)reader->bytes[offset + 1u] < 0x90u)
        return RIN_JSON_C_MALFORMED;
    if (count == 4u && first == 0xf4u &&
        (unsigned char)reader->bytes[offset + 1u] > 0x8fu)
        return RIN_JSON_C_MALFORMED;
    *bytes_out = count;
    return RIN_JSON_C_OK;
}

static RinJsonCStatus json_string_codepoint(
    RinJsonCReader* reader, uint32_t* decoded_bytes_out)
{
    uint32_t high = 0u;
    uint32_t low = 0u;
    uint32_t digit;
    uint32_t index;

    if (4u > reader->length - reader->offset)
        return RIN_JSON_C_MALFORMED;
    for (index = 0u; index < 4u; ++index) {
        if (!json_hex(reader->bytes[reader->offset++], &digit))
            return RIN_JSON_C_MALFORMED;
        high = (high << 4u) | digit;
    }
    if (high >= 0xd800u && high <= 0xdbffu) {
        if (6u > reader->length - reader->offset ||
            reader->bytes[reader->offset++] != '\\' ||
            reader->bytes[reader->offset++] != 'u')
            return RIN_JSON_C_MALFORMED;
        for (index = 0u; index < 4u; ++index) {
            if (!json_hex(reader->bytes[reader->offset++], &digit))
                return RIN_JSON_C_MALFORMED;
            low = (low << 4u) | digit;
        }
        if (low < 0xdc00u || low > 0xdfffu) return RIN_JSON_C_MALFORMED;
        *decoded_bytes_out = 4u;
        return RIN_JSON_C_OK;
    }
    if (high >= 0xdc00u && high <= 0xdfffu)
        return RIN_JSON_C_MALFORMED;
    *decoded_bytes_out = high <= 0x7fu ? 1u : high <= 0x7ffu ? 2u : 3u;
    return RIN_JSON_C_OK;
}

static RinJsonCStatus json_read_string_span_no_token(
    RinJsonCReader* reader, RinJsonCSpan* span_out)
{
    uint32_t start;
    uint32_t decoded_bytes = 0u;

    if (reader == NULL || span_out == NULL) return RIN_JSON_C_INVALID_ARGUMENT;
    rin_json_c_skip_space(reader);
    if (reader->offset >= reader->length ||
        reader->bytes[reader->offset++] != '"')
        return RIN_JSON_C_MALFORMED;
    start = reader->offset;
    while (reader->offset < reader->length) {
        const unsigned char value = (unsigned char)reader->bytes[reader->offset++];
        uint32_t bytes;
        if (value == '"') {
            span_out->data = reader->bytes + start;
            span_out->size = reader->offset - start - 1u;
            return RIN_JSON_C_OK;
        }
        if (value < 0x20u) return RIN_JSON_C_MALFORMED;
        if (value == '\\') {
            if (reader->offset >= reader->length)
                return RIN_JSON_C_MALFORMED;
            if (reader->bytes[reader->offset++] == 'u') {
                RinJsonCStatus status =
                    json_string_codepoint(reader, &bytes);
                if (status != RIN_JSON_C_OK) return status;
            } else {
                const char escaped = reader->bytes[reader->offset - 1u];
                if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                    escaped != 'b' && escaped != 'f' && escaped != 'n' &&
                    escaped != 'r' && escaped != 't')
                    return RIN_JSON_C_MALFORMED;
                bytes = 1u;
            }
        } else {
            --reader->offset;
            if (json_utf8_bytes(reader, reader->offset, &bytes) !=
                    RIN_JSON_C_OK)
                return RIN_JSON_C_MALFORMED;
            reader->offset += bytes;
        }
        if (bytes > reader->max_string_bytes ||
            decoded_bytes > reader->max_string_bytes - bytes)
            return RIN_JSON_C_CAPACITY;
        decoded_bytes += bytes;
    }
    return RIN_JSON_C_MALFORMED;
}

static RinJsonCStatus json_read_number(RinJsonCReader* reader)
{
    uint32_t start;
    uint32_t digits = 0u;

    rin_json_c_skip_space(reader);
    start = reader->offset;
    if (reader->offset < reader->length && reader->bytes[reader->offset] == '-')
        ++reader->offset;
    if (reader->offset >= reader->length) return RIN_JSON_C_MALFORMED;
    if (reader->bytes[reader->offset] == '0') {
        ++reader->offset;
        digits = 1u;
        if (reader->offset < reader->length &&
            reader->bytes[reader->offset] >= '0' &&
            reader->bytes[reader->offset] <= '9')
            return RIN_JSON_C_MALFORMED;
    } else {
        if (reader->bytes[reader->offset] < '1' ||
            reader->bytes[reader->offset] > '9')
            return RIN_JSON_C_MALFORMED;
        while (reader->offset < reader->length &&
               reader->bytes[reader->offset] >= '0' &&
               reader->bytes[reader->offset] <= '9') {
            ++reader->offset;
            ++digits;
        }
    }
    if (digits == 0u) return RIN_JSON_C_MALFORMED;
    if (reader->offset < reader->length && reader->bytes[reader->offset] == '.') {
        ++reader->offset;
        if (reader->offset >= reader->length ||
            reader->bytes[reader->offset] < '0' ||
            reader->bytes[reader->offset] > '9')
            return RIN_JSON_C_MALFORMED;
        while (reader->offset < reader->length &&
               reader->bytes[reader->offset] >= '0' &&
               reader->bytes[reader->offset] <= '9')
            ++reader->offset;
    }
    if (reader->offset < reader->length &&
        (reader->bytes[reader->offset] == 'e' ||
         reader->bytes[reader->offset] == 'E')) {
        ++reader->offset;
        if (reader->offset < reader->length &&
            (reader->bytes[reader->offset] == '+' ||
             reader->bytes[reader->offset] == '-'))
            ++reader->offset;
        if (reader->offset >= reader->length ||
            reader->bytes[reader->offset] < '0' ||
            reader->bytes[reader->offset] > '9')
            return RIN_JSON_C_MALFORMED;
        while (reader->offset < reader->length &&
               reader->bytes[reader->offset] >= '0' &&
               reader->bytes[reader->offset] <= '9')
            ++reader->offset;
    }
    return reader->offset != start ? RIN_JSON_C_OK : RIN_JSON_C_MALFORMED;
}

static RinJsonCStatus json_literal(RinJsonCReader* reader, const char* literal)
{
    uint32_t index = 0u;
    while (literal[index] != '\0') {
        if (reader->offset >= reader->length ||
            reader->bytes[reader->offset++] != literal[index])
            return RIN_JSON_C_MALFORMED;
        ++index;
    }
    return RIN_JSON_C_OK;
}

static RinJsonCStatus json_skip_value_internal(
    RinJsonCReader* reader, uint32_t depth, RinJsonCSpan* span_out)
{
    RinJsonCStatus status;
    uint32_t start;
    char value;

    if (reader == NULL) return RIN_JSON_C_INVALID_ARGUMENT;
    rin_json_c_skip_space(reader);
    if (reader->offset >= reader->length) return RIN_JSON_C_MALFORMED;
    if (depth > reader->max_depth) return RIN_JSON_C_CAPACITY;
    start = reader->offset;
    value = reader->bytes[reader->offset];
    if (value == '"') {
        status = json_token(reader);
        if (status != RIN_JSON_C_OK) return status;
        status = json_read_string_span_no_token(reader, &(RinJsonCSpan){0});
    } else if (value == '{' || value == '[') {
        const char open = value;
        const char close = value == '{' ? '}' : ']';
        ++reader->offset;
        status = json_token(reader);
        if (status != RIN_JSON_C_OK) return status;
        rin_json_c_skip_space(reader);
        if (reader->offset < reader->length &&
            reader->bytes[reader->offset] == close) {
            ++reader->offset;
        } else {
            for (;;) {
                if (open == '{') {
                    status = json_token(reader);
                    if (status != RIN_JSON_C_OK) return status;
                    status = json_read_string_span_no_token(
                        reader, &(RinJsonCSpan){0});
                    if (status != RIN_JSON_C_OK) return status;
                    rin_json_c_skip_space(reader);
                    status = rin_json_c_consume(reader, ':');
                    if (status != RIN_JSON_C_OK) return status;
                }
                status = json_skip_value_internal(reader, depth + 1u, NULL);
                if (status != RIN_JSON_C_OK) return status;
                rin_json_c_skip_space(reader);
                if (reader->offset >= reader->length)
                    return RIN_JSON_C_MALFORMED;
                if (reader->bytes[reader->offset] == close) {
                    ++reader->offset;
                    break;
                }
                if (reader->bytes[reader->offset++] != ',')
                    return RIN_JSON_C_MALFORMED;
                rin_json_c_skip_space(reader);
            }
        }
    } else if (value == 't' || value == 'f' || value == 'n') {
        const char* literal = value == 't' ? "true" :
                              value == 'f' ? "false" : "null";
        status = json_token(reader);
        if (status != RIN_JSON_C_OK) return status;
        status = json_literal(reader, literal);
    } else {
        status = json_token(reader);
        if (status != RIN_JSON_C_OK) return status;
        status = json_read_number(reader);
    }
    if (status != RIN_JSON_C_OK) return status;
    if (span_out != NULL) {
        span_out->data = reader->bytes + start;
        span_out->size = reader->offset - start;
    }
    return RIN_JSON_C_OK;
}

RinJsonCStatus rin_json_c_reader_init(
    RinJsonCReader* reader, const char* bytes, uint32_t length,
    uint32_t max_bytes, uint32_t max_depth, uint32_t max_tokens,
    uint32_t max_string_bytes)
{
    if (reader != NULL) *reader = (RinJsonCReader){0};
    if (reader == NULL || bytes == NULL || length == 0u || max_bytes == 0u ||
        length > max_bytes || max_depth == 0u || max_tokens == 0u ||
        max_string_bytes == 0u)
        return RIN_JSON_C_INVALID_ARGUMENT;
    reader->bytes = bytes;
    reader->length = length;
    reader->offset = 0u;
    reader->max_depth = max_depth;
    reader->max_tokens = max_tokens;
    reader->max_string_bytes = max_string_bytes;
    reader->tokens = 0u;
    return RIN_JSON_C_OK;
}

void rin_json_c_skip_space(RinJsonCReader* reader)
{
    if (reader == NULL) return;
    while (reader->offset < reader->length &&
           json_is_space(reader->bytes[reader->offset]))
        ++reader->offset;
}

RinJsonCStatus rin_json_c_consume(RinJsonCReader* reader, char expected)
{
    if (reader == NULL) return RIN_JSON_C_INVALID_ARGUMENT;
    rin_json_c_skip_space(reader);
    if (reader->offset >= reader->length ||
        reader->bytes[reader->offset] != expected)
        return RIN_JSON_C_MALFORMED;
    ++reader->offset;
    return RIN_JSON_C_OK;
}

RinJsonCStatus rin_json_c_read_string_span(
    RinJsonCReader* reader, RinJsonCSpan* span_out)
{
    RinJsonCStatus status;
    if (span_out != NULL) *span_out = (RinJsonCSpan){0};
    if (reader == NULL || span_out == NULL) return RIN_JSON_C_INVALID_ARGUMENT;
    status = json_token(reader);
    if (status != RIN_JSON_C_OK) return status;
    return json_read_string_span_no_token(reader, span_out);
}

RinJsonCStatus rin_json_c_read_ascii_string(
    RinJsonCReader* reader, char* output, uint32_t capacity,
    uint32_t* bytes_out)
{
    RinJsonCSpan span;
    RinJsonCStatus status;
    uint32_t index;

    if (bytes_out != NULL) *bytes_out = 0u;
    if (reader == NULL || output == NULL || capacity == 0u)
        return RIN_JSON_C_INVALID_ARGUMENT;
    output[0] = '\0';
    status = rin_json_c_read_string_span(reader, &span);
    if (status != RIN_JSON_C_OK) return status;
    if (span.size == UINT32_MAX || span.size >= capacity)
        return RIN_JSON_C_CAPACITY;
    for (index = 0u; index < span.size; ++index) {
        const unsigned char value = (unsigned char)span.data[index];
        if (value < 0x20u || value > 0x7eu || value == '\\') {
            output[0] = '\0';
            return RIN_JSON_C_MALFORMED;
        }
        output[index] = (char)value;
    }
    output[span.size] = '\0';
    if (bytes_out != NULL) *bytes_out = span.size;
    return RIN_JSON_C_OK;
}

RinJsonCStatus rin_json_c_read_u32(
    RinJsonCReader* reader, uint32_t* value_out)
{
    uint32_t value = 0u;
    uint32_t digits = 0u;
    RinJsonCStatus status;

    if (value_out != NULL) *value_out = 0u;
    if (reader == NULL || value_out == NULL) return RIN_JSON_C_INVALID_ARGUMENT;
    rin_json_c_skip_space(reader);
    if (reader->offset >= reader->length ||
        reader->bytes[reader->offset] < '0' ||
        reader->bytes[reader->offset] > '9')
        return RIN_JSON_C_MALFORMED;
    status = json_token(reader);
    if (status != RIN_JSON_C_OK) return status;
    if (reader->bytes[reader->offset] == '0') {
        ++reader->offset;
        digits = 1u;
        if (reader->offset < reader->length &&
            reader->bytes[reader->offset] >= '0' &&
            reader->bytes[reader->offset] <= '9')
            return RIN_JSON_C_MALFORMED;
    } else {
        while (reader->offset < reader->length &&
               reader->bytes[reader->offset] >= '0' &&
               reader->bytes[reader->offset] <= '9') {
            const uint32_t digit =
                (uint32_t)(reader->bytes[reader->offset] - '0');
            if (value > (UINT32_MAX - digit) / 10u)
                return RIN_JSON_C_MALFORMED;
            value = value * 10u + digit;
            ++reader->offset;
            ++digits;
        }
    }
    if (digits == 0u) return RIN_JSON_C_MALFORMED;
    *value_out = value;
    return RIN_JSON_C_OK;
}

RinJsonCStatus rin_json_c_skip_value(
    RinJsonCReader* reader, RinJsonCSpan* span_out)
{
    if (span_out != NULL) *span_out = (RinJsonCSpan){0};
    return json_skip_value_internal(reader, 0u, span_out);
}

RinJsonCStatus rin_json_c_document_complete(RinJsonCReader* reader)
{
    if (reader == NULL) return RIN_JSON_C_INVALID_ARGUMENT;
    rin_json_c_skip_space(reader);
    return reader->offset == reader->length
        ? RIN_JSON_C_OK : RIN_JSON_C_MALFORMED;
}
