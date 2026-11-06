#ifndef MANGEN_MATCHER_H
#define MANGEN_MATCHER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mangen_matcher mangen_matcher_t;

typedef enum {
    MANGEN_MATCHER_OK = 0,
    MANGEN_MATCHER_INVALID_ARGUMENT,
    MANGEN_MATCHER_INVALID_PATTERN,
    MANGEN_MATCHER_OUT_OF_MEMORY,
    MANGEN_MATCHER_LIMIT_EXCEEDED
} mangen_matcher_status_t;

/*
 * Compiles a complete-basename pattern.
 *
 * Pattern syntax:
 *   '.'  matches exactly one Unicode grapheme cluster;
 *   '*'  matches zero or more grapheme clusters;
 *   '\'  escapes the following grapheme cluster.
 *
 * Literal runs are normalized to NFC.
 * Matching is case-sensitive.
 * Invalid UTF-8 bytes are preserved.
 *
 * The resulting matcher owns a minimized DFA.
 *
 * On success:
 *   returns MANGEN_MATCHER_OK;
 *   *out receives an owned matcher.
 *
 * On failure:
 *   *out is NULL.
 */
mangen_matcher_status_t mangen_matcher_compile(
    const char *pattern,
    mangen_matcher_t **out
);

/*
 * Matches the complete basename.
 *
 * The matcher is not modified.
 * Unicode normalization may allocate memory.
 *
 * On success:
 *   returns MANGEN_MATCHER_OK;
 *   *matched contains the result.
 *
 * On failure:
 *   *matched is false.
 */
mangen_matcher_status_t mangen_matcher_match(
    const mangen_matcher_t *matcher,
    const char *basename,
    bool *matched
);

/*
 * Convenience wrapper.
 *
 * Returns false both on mismatch and on an internal error.
 * Use mangen_matcher_match() when errors must be distinguished.
 */
bool mangen_matcher_matches(
    const mangen_matcher_t *matcher,
    const char *basename
);

/*
 * Returns the number of states in the minimized DFA.
 * Returns zero for NULL.
 */
size_t mangen_matcher_state_count(
    const mangen_matcher_t *matcher
);

/*
 * Releases the compiled matcher.
 * Accepts NULL.
 *
 * Must not run concurrently with matching on the same object.
 */
void mangen_matcher_free(
    mangen_matcher_t *matcher
);

#ifdef __cplusplus
}
#endif

#endif
