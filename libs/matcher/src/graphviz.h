#ifndef MANGEN_MATCHER_GRAPHVIZ_H
#define MANGEN_MATCHER_GRAPHVIZ_H

#include "dfa.h"
#include "nfa.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * Writes Graphviz DOT to an already opened stream.
 *
 * The stream remains owned by the caller.
 * Returns false on invalid input, allocation failure or I/O error.
 */
bool matcher_graphviz_write_nfa(
    const matcher_nfa_t *nfa,
    FILE *out
);

bool matcher_graphviz_write_dfa(
    const matcher_dfa_t *dfa,
    FILE *out
);

bool matcher_graphviz_write_min_dfa(
    const matcher_dfa_t *dfa,
    FILE *out
);

#endif
