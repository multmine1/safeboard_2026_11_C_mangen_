#include "utf8.h"

#include <utf8proc.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static utf8proc_ssize_t matcher_utf8_decode(
    const unsigned char *data,
    size_t remaining,
    utf8proc_int32_t *codepoint
)
{
    const size_t available = remaining < 4u ? remaining : 4u;

    return utf8proc_iterate(
        data,
        (utf8proc_ssize_t)available,
        codepoint
    );
}

static mangen_matcher_status_t matcher_utf8_append(
    unsigned char **buffer,
    size_t *length,
    size_t *capacity,
    const unsigned char *source,
    size_t count
)
{
    if (count > SIZE_MAX - *length - 1u) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    const size_t required = *length + count + 1u;

    if (required > *capacity) {
        size_t new_capacity = *capacity;

        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2u) {
                new_capacity = required;
                break;
            }

            new_capacity *= 2u;
        }

        unsigned char *resized = realloc(
            *buffer,
            new_capacity
        );

        if (resized == NULL) {
            return MANGEN_MATCHER_OUT_OF_MEMORY;
        }

        *buffer = resized;
        *capacity = new_capacity;
    }

    if (count != 0u) {
        memcpy(*buffer + *length, source, count);
        *length += count;
    }

    (*buffer)[*length] = '\0';

    return MANGEN_MATCHER_OK;
}

mangen_matcher_status_t matcher_utf8_normalize_nfc(
    const unsigned char *data,
    size_t length,
    matcher_utf8_text_t *out
)
{
    if (
        out == NULL ||
        out->data != NULL ||
        out->length != 0u ||
        (data == NULL && length != 0u)
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    if (length == SIZE_MAX) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    size_t capacity = length + 1u;
    size_t used = 0u;

    unsigned char *buffer = malloc(capacity);

    if (buffer == NULL) {
        return MANGEN_MATCHER_OUT_OF_MEMORY;
    }

    buffer[0] = '\0';

    size_t offset = 0u;

    while (offset < length) {
        utf8proc_int32_t codepoint = 0;

        utf8proc_ssize_t width = matcher_utf8_decode(
            data + offset,
            length - offset,
            &codepoint
        );

        if (width < 0) {
            const mangen_matcher_status_t status =
                matcher_utf8_append(
                    &buffer,
                    &used,
                    &capacity,
                    data + offset,
                    1u
                );

            if (status != MANGEN_MATCHER_OK) {
                free(buffer);
                return status;
            }

            ++offset;
            continue;
        }

        const size_t run_start = offset;

        do {
            offset += (size_t)width;

            if (offset == length) {
                break;
            }

            width = matcher_utf8_decode(
                data + offset,
                length - offset,
                &codepoint
            );

        } while (width > 0);

        const size_t run_length = offset - run_start;

        if (run_length > (size_t)PTRDIFF_MAX) {
            free(buffer);
            return MANGEN_MATCHER_LIMIT_EXCEEDED;
        }

        utf8proc_uint8_t *normalized = NULL;

        const utf8proc_ssize_t normalized_length =
            utf8proc_map(
                data + run_start,
                (utf8proc_ssize_t)run_length,
                &normalized,
                UTF8PROC_STABLE | UTF8PROC_COMPOSE
            );

        if (normalized_length < 0) {
            free(buffer);

            return normalized_length == UTF8PROC_ERROR_NOMEM
                ? MANGEN_MATCHER_OUT_OF_MEMORY
                : MANGEN_MATCHER_INVALID_ARGUMENT;
        }

        const mangen_matcher_status_t status =
            matcher_utf8_append(
                &buffer,
                &used,
                &capacity,
                normalized,
                (size_t)normalized_length
            );

        utf8proc_free(normalized);

        if (status != MANGEN_MATCHER_OK) {
            free(buffer);
            return status;
        }
    }

    out->data = buffer;
    out->length = used;

    return MANGEN_MATCHER_OK;
}

void matcher_utf8_text_free(matcher_utf8_text_t *text)
{
    if (text == NULL) {
        return;
    }

    free(text->data);

    text->data = NULL;
    text->length = 0u;
}

void matcher_utf8_cursor_init(
    matcher_utf8_cursor_t *cursor,
    const unsigned char *data,
    size_t length
)
{
    if (cursor == NULL) {
        return;
    }

    cursor->data = data;
    cursor->length = length;
    cursor->offset = 0u;
    cursor->break_state = 0;
}

bool matcher_utf8_next(
    matcher_utf8_cursor_t *cursor,
    matcher_utf8_grapheme_t *out
)
{
    if (out != NULL) {
        out->data = NULL;
        out->length = 0u;
        out->valid_utf8 = false;
    }

    if (
        cursor == NULL ||
        out == NULL ||
        (cursor->data == NULL && cursor->length != 0u) ||
        cursor->offset >= cursor->length
    ) {
        return false;
    }

    const size_t start = cursor->offset;

    utf8proc_int32_t previous = 0;

    const utf8proc_ssize_t first_width =
        matcher_utf8_decode(
            cursor->data + start,
            cursor->length - start,
            &previous
        );

    if (first_width < 0) {
        out->data = cursor->data + start;
        out->length = 1u;
        out->valid_utf8 = false;

        cursor->offset = start + 1u;
        cursor->break_state = 0;

        return true;
    }

    size_t end = start + (size_t)first_width;

    while (end < cursor->length) {
        utf8proc_int32_t current = 0;

        const utf8proc_ssize_t width =
            matcher_utf8_decode(
                cursor->data + end,
                cursor->length - end,
                &current
            );

        if (width < 0) {
            cursor->break_state = 0;
            break;
        }

        const utf8proc_bool boundary =
            utf8proc_grapheme_break_stateful(
                previous,
                current,
                &cursor->break_state
            );

        if (boundary) {
            break;
        }

        end += (size_t)width;
        previous = current;
    }

    out->data = cursor->data + start;
    out->length = end - start;
    out->valid_utf8 = true;

    cursor->offset = end;

    return true;
}
