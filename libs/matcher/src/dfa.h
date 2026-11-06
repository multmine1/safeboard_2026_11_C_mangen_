#ifndef MANGEN_MATCHER_DFA_H
#define MANGEN_MATCHER_DFA_H

#include "nfa.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t matcher_dfa_state_id_t;

typedef struct {
    uint32_t literal_offset;
    uint32_t literal_length;
} matcher_dfa_class_t;

typedef struct {
    matcher_dfa_state_id_t *transitions;
    uint8_t *accepting;

    matcher_dfa_class_t *classes;
    unsigned char *literals;
    size_t literals_length;

    uint32_t state_count;
    uint32_t class_count;
    matcher_dfa_state_id_t start_state;
} matcher_dfa_t;

/*
 * Builds a deterministic automaton using subset construction.
 *
 * Alphabet:
 *   class 0    - every grapheme not present as a literal.
 *   classes 1+ - unique literal graphemes.
 *
 * Transitions:
 *   transitions[state * class_count + class_id]
 *
 * The resulting DFA is complete:
 * every state has one transition for every class.
 *
 * The DFA owns all its memory and does not depend on the
 * lifetime of the source NFA.
 *
 * The output must be zero-initialized.
 * On failure it remains empty.
 */
mangen_matcher_status_t matcher_dfa_build(
    const matcher_nfa_t *nfa,
    matcher_dfa_t *out
);

/*
 * Maps a normalized grapheme to its alphabet class.
 *
 * Returns 0 when the grapheme is not a literal class.
 * The input must already be NFC-normalized.
 */
uint32_t matcher_dfa_class_of(
    const matcher_dfa_t *dfa,
    const unsigned char *grapheme,
    size_t length
);

/*
 * Checks whether a DFA state is accepting.
 * Returns false for invalid state IDs.
 */
bool matcher_dfa_is_accepting(
    const matcher_dfa_t *dfa,
    uint32_t state
);

/*
 * Releases all owned memory.
 * NULL is accepted.
 */
void matcher_dfa_free(matcher_dfa_t *dfa);

#ifdef __cplusplus
}
#endif

#endif
