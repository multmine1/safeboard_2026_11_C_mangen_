#ifndef MANGEN_MATCHER_INTERNAL_H
#define MANGEN_MATCHER_INTERNAL_H

#include "mangen/matcher.h"

/* Limits are checked before allocating the respective automaton. */
#define MANGEN_MAX_PATTERN_BYTES 1024u
#define MANGEN_MAX_DFA_STATES 4096u
#define MANGEN_ALPHABET_SIZE 256u

#endif
