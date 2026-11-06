#include "pattern.h"
#include "utf8.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static mangen_matcher_status_t matcher_pattern_add_literal(
    matcher_pattern_t *pattern,
    size_t token_limit,
    size_t *literal_capacity,
    const matcher_utf8_grapheme_t *grapheme
)
{
    if (pattern->count >= token_limit) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    if (grapheme->length > SIZE_MAX - pattern->literals_length) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    const size_t required =
        pattern->literals_length + grapheme->length;

    if (required > *literal_capacity) {
        size_t new_capacity =
            *literal_capacity != 0u ? *literal_capacity : 16u;

        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2u) {
                new_capacity = required;
                break;
            }

            new_capacity *= 2u;
        }

        unsigned char *resized = realloc(
            pattern->literals,
            new_capacity
        );

        if (resized == NULL) {
            return MANGEN_MATCHER_OUT_OF_MEMORY;
        }

        pattern->literals = resized;
        *literal_capacity = new_capacity;
    }

    const size_t offset = pattern->literals_length;

    memcpy(
        pattern->literals + offset,
        grapheme->data,
        grapheme->length
    );

    pattern->literals_length = required;

    matcher_token_t token = {
        .kind = MATCHER_TOKEN_LITERAL,
        .literal_offset = offset,
        .literal_length = grapheme->length
    };

    pattern->tokens[pattern->count++] = token;

    return MANGEN_MATCHER_OK;
}

static mangen_matcher_status_t matcher_pattern_flush_literals(
    matcher_pattern_t *pattern,
    size_t token_limit,
    size_t *literal_capacity,
    const unsigned char *raw,
    size_t *raw_length
)
{
    if (*raw_length == 0u) {
        return MANGEN_MATCHER_OK;
    }

    matcher_utf8_text_t normalized = {0};

    mangen_matcher_status_t status =
        matcher_utf8_normalize_nfc(
            raw,
            *raw_length,
            &normalized
        );

    if (status != MANGEN_MATCHER_OK) {
        return status;
    }

    matcher_utf8_cursor_t cursor = {0};

    matcher_utf8_cursor_init(
        &cursor,
        normalized.data,
        normalized.length
    );

    matcher_utf8_grapheme_t grapheme = {0};

    while (matcher_utf8_next(&cursor, &grapheme)) {
        status = matcher_pattern_add_literal(
            pattern,
            token_limit,
            literal_capacity,
            &grapheme
        );

        if (status != MANGEN_MATCHER_OK) {
            break;
        }
    }

    matcher_utf8_text_free(&normalized);

    if (status == MANGEN_MATCHER_OK) {
        *raw_length = 0u;
    }

    return status;
}

mangen_matcher_status_t matcher_pattern_parse(
    const char *source,
    matcher_pattern_t *out
)
{
    if (
        source == NULL ||
        out == NULL ||
        out->tokens != NULL ||
        out->count != 0u ||
        out->literals != NULL ||
        out->literals_length != 0u
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    size_t length = 0u;

    while (
        length <= MANGEN_MAX_PATTERN_BYTES &&
        source[length] != '\0'
    ) {
        ++length;
    }

    if (length > MANGEN_MAX_PATTERN_BYTES) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    matcher_pattern_t parsed = {0};

    unsigned char *raw = NULL;

    if (length != 0u) {
        parsed.tokens = calloc(
            length,
            sizeof(*parsed.tokens)
        );

        raw = malloc(length);

        if (parsed.tokens == NULL || raw == NULL) {
            free(raw);
            matcher_pattern_free(&parsed);

            return MANGEN_MATCHER_OUT_OF_MEMORY;
        }
    }

    size_t raw_length = 0u;
    size_t literal_capacity = 0u;

    mangen_matcher_status_t status = MANGEN_MATCHER_OK;

    size_t i = 0u;

    while (i < length) {
        const unsigned char byte =
            (unsigned char)source[i];

        if (byte == (unsigned char)'\\') {
            ++i;

            if (i == length) {
                status = MANGEN_MATCHER_INVALID_PATTERN;
                break;
            }

            matcher_utf8_cursor_t escaped = {0};

            matcher_utf8_cursor_init(
                &escaped,
                (const unsigned char *)source + i,
                length - i
            );

            matcher_utf8_grapheme_t grapheme = {0};

            if (!matcher_utf8_next(&escaped, &grapheme)) {
                status = MANGEN_MATCHER_INVALID_PATTERN;
                break;
            }

            memcpy(
                raw + raw_length,
                grapheme.data,
                grapheme.length
            );

            raw_length += grapheme.length;
            i += grapheme.length;

            continue;
        }

        if (
            byte == (unsigned char)'.' ||
            byte == (unsigned char)'*'
        ) {
            status = matcher_pattern_flush_literals(
                &parsed,
                length,
                &literal_capacity,
                raw,
                &raw_length
            );

            if (status != MANGEN_MATCHER_OK) {
                break;
            }

            const matcher_token_kind_t kind =
                byte == (unsigned char)'.'
                    ? MATCHER_TOKEN_ANY_ONE
                    : MATCHER_TOKEN_ANY_MANY;

            const bool repeated_star =
                kind == MATCHER_TOKEN_ANY_MANY &&
                parsed.count != 0u &&
                parsed.tokens[parsed.count - 1u].kind ==
                    MATCHER_TOKEN_ANY_MANY;

            if (!repeated_star) {
                if (parsed.count >= length) {
                    status = MANGEN_MATCHER_LIMIT_EXCEEDED;
                    break;
                }

                parsed.tokens[parsed.count++] = (matcher_token_t){
                    .kind = kind,
                    .literal_offset = 0u,
                    .literal_length = 0u
                };
            }

            ++i;
            continue;
        }

        raw[raw_length++] = byte;
        ++i;
    }

    if (status == MANGEN_MATCHER_OK) {
        status = matcher_pattern_flush_literals(
            &parsed,
            length,
            &literal_capacity,
            raw,
            &raw_length
        );
    }

    free(raw);

    if (status == MANGEN_MATCHER_OK) {
        *out = parsed;
    } else {
        matcher_pattern_free(&parsed);
    }

    return status;
}

void matcher_pattern_free(matcher_pattern_t *pattern)
{
    if (pattern == NULL) {
        return;
    }

    free(pattern->tokens);
    free(pattern->literals);

    pattern->tokens = NULL;
    pattern->count = 0u;

    pattern->literals = NULL;
    pattern->literals_length = 0u;
}
