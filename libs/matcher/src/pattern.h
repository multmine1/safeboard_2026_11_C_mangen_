#ifndef MANGEN_MATCHER_PATTERN_H
#define MANGEN_MATCHER_PATTERN_H

#include "matcher_internal.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MATCHER_TOKEN_LITERAL = 0,
    MATCHER_TOKEN_ANY_ONE,
    MATCHER_TOKEN_ANY_MANY
} matcher_token_kind_t;

typedef struct {
    matcher_token_kind_t kind;

    size_t literal_offset;
    size_t literal_length;
} matcher_token_t;

typedef struct {
    matcher_token_t *tokens;
    size_t count;

    unsigned char *literals;
    size_t literals_length;
} matcher_pattern_t;

/*
 * Parses a wildcard pattern.
 *
 * Syntax:
 *   .  - exactly one extended grapheme cluster
 *   *  - zero or more extended grapheme clusters
 *   \  - escapes the next grapheme cluster
 *
 * All literal runs are normalized to NFC.
 * Consecutive unescaped '*' operators are collapsed.
 *
 * Input:
 *   source - NUL-terminated pattern string.
 *
 * Output:
 *   out->tokens          - token array.
 *   out->count           - number of tokens.
 *   out->literals        - contiguous literal storage.
 *   out->literals_length - storage size in bytes.
 *
 * The caller must zero-initialize *out.
 * On failure, *out remains empty.
 *
 * Maximum input size:
 *   MANGEN_MAX_PATTERN_BYTES.
 */
mangen_matcher_status_t matcher_pattern_parse(
    const char *source,
    matcher_pattern_t *out
);

/*
 * Releases all owned memory and resets the structure.
 * NULL is accepted.
 */
void matcher_pattern_free(matcher_pattern_t *pattern);

#ifdef __cplusplus
}
#endif

#endif
