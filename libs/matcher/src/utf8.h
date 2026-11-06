#ifndef MANGEN_MATCHER_UTF8_H
#define MANGEN_MATCHER_UTF8_H

#include "mangen/matcher.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *data;
    size_t length;
} matcher_utf8_text_t;

typedef struct {
    const unsigned char *data;
    size_t length;
    size_t offset;
    int32_t break_state;
} matcher_utf8_cursor_t;

typedef struct {
    const unsigned char *data;
    size_t length;
    bool valid_utf8;
} matcher_utf8_grapheme_t;

/*
 * Creates an NFC-normalized copy of the input.
 *
 * Valid UTF-8 spans are normalized independently.
 * Invalid UTF-8 bytes are preserved without modification.
 *
 * Input:
 *   data   - input bytes; may be NULL when length == 0.
 *   length - input length in bytes.
 *
 * Output:
 *   out->data   - allocated, NUL-terminated buffer.
 *   out->length - resulting length, excluding the final NUL.
 *
 * The output may contain embedded NUL bytes when the input does.
 * The caller must initialize *out to zero.
 *
 * On failure, *out remains empty.
 */
mangen_matcher_status_t matcher_utf8_normalize_nfc(
    const unsigned char *data,
    size_t length,
    matcher_utf8_text_t *out
);

/*
 * Releases a normalized buffer and clears its fields.
 * NULL is accepted.
 */
void matcher_utf8_text_free(matcher_utf8_text_t *text);

/*
 * Initializes a cursor over a byte sequence.
 *
 * The underlying data must remain valid while the cursor is used.
 * Call this function separately for every new input string.
 */
void matcher_utf8_cursor_init(
    matcher_utf8_cursor_t *cursor,
    const unsigned char *data,
    size_t length
);

/*
 * Reads one extended grapheme cluster.
 *
 * For valid UTF-8, segmentation follows Unicode UAX #29.
 * Each invalid UTF-8 byte is returned separately.
 *
 * Returns:
 *   true  - one element was read.
 *   false - end of input or invalid function arguments.
 *
 * The returned slice refers to the original input buffer.
 * No allocation is performed.
 */
bool matcher_utf8_next(
    matcher_utf8_cursor_t *cursor,
    matcher_utf8_grapheme_t *out
);

#ifdef __cplusplus
}
#endif

#endif
