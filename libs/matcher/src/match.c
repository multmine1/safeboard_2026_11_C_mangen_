#include "mangen/matcher.h"

#include "dfa.h"
#include "min_dfa.h"
#include "nfa.h"
#include "pattern.h"
#include "utf8.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct mangen_matcher {
    matcher_dfa_t dfa;
};

static bool matcher_is_ascii(
    const unsigned char *data,
    size_t length
) {
    for (size_t i = 0; i < length; ++i) {
        if (data[i] >= 0x80u) {
            return false;
        }
    }

    return true;
}

mangen_matcher_status_t mangen_matcher_compile(
    const char *pattern,
    mangen_matcher_t **out
) {
    if (out == NULL) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    *out = NULL;

    if (pattern == NULL) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    matcher_pattern_t parsed = {0};
    matcher_nfa_t nfa = {0};
    matcher_dfa_t dfa = {0};
    matcher_dfa_t minimized = {0};

    mangen_matcher_status_t status =
        matcher_pattern_parse(pattern, &parsed);

    if (status != MANGEN_MATCHER_OK) {
        goto cleanup;
    }

    status = matcher_nfa_build(&parsed, &nfa);

    if (status != MANGEN_MATCHER_OK) {
        goto cleanup;
    }

    status = matcher_dfa_build(&nfa, &dfa);

    if (status != MANGEN_MATCHER_OK) {
        goto cleanup;
    }

    status = matcher_min_dfa_build(&dfa, &minimized);

    if (status != MANGEN_MATCHER_OK) {
        goto cleanup;
    }

    mangen_matcher_t *compiled = malloc(sizeof(*compiled));

    if (compiled == NULL) {
        status = MANGEN_MATCHER_OUT_OF_MEMORY;
        goto cleanup;
    }

    compiled->dfa = minimized;

    memset(&minimized, 0, sizeof(minimized));

    *out = compiled;

cleanup:
    matcher_dfa_free(&minimized);
    matcher_dfa_free(&dfa);
    matcher_nfa_free(&nfa);
    matcher_pattern_free(&parsed);

    return status;
}

mangen_matcher_status_t mangen_matcher_match(
    const mangen_matcher_t *matcher,
    const char *basename,
    bool *matched
) {
    if (matched == NULL) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    *matched = false;

    if (matcher == NULL || basename == NULL) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    const matcher_dfa_t *dfa = &matcher->dfa;

    const unsigned char *data =
        (const unsigned char *)basename;

    size_t length = strlen(basename);

    matcher_utf8_text_t normalized = {0};

    /*
     * ASCII is already NFC.
     * Avoid allocating a normalized copy for ASCII-only input.
     */
    if (!matcher_is_ascii(data, length)) {
        mangen_matcher_status_t status =
            matcher_utf8_normalize_nfc(
                data,
                length,
                &normalized
            );

        if (status != MANGEN_MATCHER_OK) {
            return status;
        }

        data = normalized.data;
        length = normalized.length;
    }

    matcher_utf8_cursor_t cursor;

    matcher_utf8_cursor_init(
        &cursor,
        data,
        length
    );

    matcher_dfa_state_id_t state = dfa->start_state;

    matcher_utf8_grapheme_t grapheme;

    while (matcher_utf8_next(&cursor, &grapheme)) {
        const uint32_t symbol_class =
            matcher_dfa_class_of(
                dfa,
                grapheme.data,
                grapheme.length
            );

        const size_t transition_index =
            (size_t)state * dfa->class_count +
            symbol_class;

        state = dfa->transitions[transition_index];
    }

    *matched = matcher_dfa_is_accepting(
        dfa,
        state
    );

    matcher_utf8_text_free(&normalized);

    return MANGEN_MATCHER_OK;
}

bool mangen_matcher_matches(
    const mangen_matcher_t *matcher,
    const char *basename
) {
    bool matched = false;

    const mangen_matcher_status_t status =
        mangen_matcher_match(
            matcher,
            basename,
            &matched
        );

    return status == MANGEN_MATCHER_OK && matched;
}

size_t mangen_matcher_state_count(
    const mangen_matcher_t *matcher
) {
    if (matcher == NULL) {
        return 0;
    }

    return matcher->dfa.state_count;
}

void mangen_matcher_free(
    mangen_matcher_t *matcher
) {
    if (matcher == NULL) {
        return;
    }

    matcher_dfa_free(&matcher->dfa);

    free(matcher);
}
