#ifndef MANGEN_MATCHER_MIN_DFA_H
#define MANGEN_MATCHER_MIN_DFA_H

#include "dfa.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimizes a complete DFA using Hopcroft partition refinement.
 *
 * Input:
 *   dfa - complete deterministic automaton.
 *
 * Output:
 *   out - minimized DFA.
 *
 * Properties:
 *   - recognizes exactly the same language;
 *   - preserves all input alphabet classes;
 *   - contains only reachable states;
 *   - assigns state 0 as the initial state;
 *   - owns its transitions, classes and literal storage.
 *
 * The output must be zero-initialized.
 * On failure it remains empty.
 *
 * Input and output must not be the same object.
 */
mangen_matcher_status_t matcher_min_dfa_build(
    const matcher_dfa_t *dfa,
    matcher_dfa_t *out
);

#ifdef __cplusplus
}
#endif

#endif
