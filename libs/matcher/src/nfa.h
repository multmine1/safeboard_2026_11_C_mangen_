#ifndef MANGEN_MATCHER_NFA_H
#define MANGEN_MATCHER_NFA_H

#include "pattern.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MATCHER_NFA_LITERAL = 0,
    MATCHER_NFA_ANY_ONE,
    MATCHER_NFA_ANY_MANY
} matcher_nfa_edge_kind_t;

typedef struct {
    uint32_t to;
    matcher_nfa_edge_kind_t kind;

    uint32_t literal_offset;
    uint32_t literal_length;
} matcher_nfa_edge_t;

typedef struct {
    matcher_nfa_edge_t *edges;
    uint32_t *edge_offsets;

    uint32_t state_count;
    uint32_t edge_count;

    uint32_t start_state;
    uint32_t accept_state;

    unsigned char *literals;
    size_t literals_length;
} matcher_nfa_t;

/*
 * Builds an epsilon-free NFA from parsed pattern tokens.
 *
 * A literal:
 *   current --literal--> current + 1
 *
 * ANY_ONE:
 *   current --ANY--> current + 1
 *
 * ANY_MANY:
 *   current --ANY--> current
 *
 * The NFA owns its edges, offsets and literal storage.
 * It remains valid after matcher_pattern_free().
 *
 * The caller must zero-initialize *out.
 * On failure, *out remains empty.
 */
mangen_matcher_status_t matcher_nfa_build(
    const matcher_pattern_t *pattern,
    matcher_nfa_t *out
);

/*
 * Convenience wrapper:
 *   parse pattern -> build NFA -> free parsed pattern.
 */
mangen_matcher_status_t matcher_nfa_from_pattern(
    const char *source,
    matcher_nfa_t *out
);

/*
 * Releases all owned memory and clears the structure.
 * NULL is accepted.
 */
void matcher_nfa_free(matcher_nfa_t *nfa);

#ifdef __cplusplus
}
#endif

#endif
