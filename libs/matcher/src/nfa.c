#include "nfa.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool matcher_nfa_is_empty(const matcher_nfa_t *nfa)
{
    return
        nfa->edges == NULL &&
        nfa->edge_offsets == NULL &&
        nfa->state_count == 0u &&
        nfa->edge_count == 0u &&
        nfa->start_state == 0u &&
        nfa->accept_state == 0u &&
        nfa->literals == NULL &&
        nfa->literals_length == 0u;
}

static mangen_matcher_status_t matcher_nfa_validate_pattern(
    const matcher_pattern_t *pattern,
    uint32_t *consuming_count
)
{
    if (pattern->count > MANGEN_MAX_PATTERN_BYTES) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    if (
        (pattern->count != 0u && pattern->tokens == NULL) ||
        (pattern->literals_length != 0u &&
         pattern->literals == NULL)
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    if (pattern->literals_length > UINT32_MAX) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    uint32_t consuming = 0u;

    for (size_t i = 0u; i < pattern->count; ++i) {
        const matcher_token_t *token = &pattern->tokens[i];

        switch (token->kind) {
        case MATCHER_TOKEN_LITERAL:
            if (
                token->literal_length == 0u ||
                token->literal_offset > pattern->literals_length ||
                token->literal_length >
                    pattern->literals_length - token->literal_offset
            ) {
                return MANGEN_MATCHER_INVALID_ARGUMENT;
            }

            if (
                token->literal_offset > UINT32_MAX ||
                token->literal_length > UINT32_MAX
            ) {
                return MANGEN_MATCHER_LIMIT_EXCEEDED;
            }

            ++consuming;
            break;

        case MATCHER_TOKEN_ANY_ONE:
            ++consuming;
            break;

        case MATCHER_TOKEN_ANY_MANY:
            break;

        default:
            return MANGEN_MATCHER_INVALID_ARGUMENT;
        }
    }

    *consuming_count = consuming;

    return MANGEN_MATCHER_OK;
}

mangen_matcher_status_t matcher_nfa_build(
    const matcher_pattern_t *pattern,
    matcher_nfa_t *out
)
{
    if (
        pattern == NULL ||
        out == NULL ||
        !matcher_nfa_is_empty(out)
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    uint32_t consuming_count = 0u;

    const mangen_matcher_status_t status =
        matcher_nfa_validate_pattern(
            pattern,
            &consuming_count
        );

    if (status != MANGEN_MATCHER_OK) {
        return status;
    }

    matcher_nfa_t result = {0};

    result.state_count = consuming_count + 1u;
    result.edge_count = (uint32_t)pattern->count;

    result.start_state = 0u;
    result.accept_state = consuming_count;

    result.edge_offsets = calloc(
        (size_t)result.state_count + 1u,
        sizeof(*result.edge_offsets)
    );

    if (result.edge_offsets == NULL) {
        return MANGEN_MATCHER_OUT_OF_MEMORY;
    }

    if (result.edge_count != 0u) {
        result.edges = calloc(
            result.edge_count,
            sizeof(*result.edges)
        );

        if (result.edges == NULL) {
            matcher_nfa_free(&result);
            return MANGEN_MATCHER_OUT_OF_MEMORY;
        }
    }

    if (pattern->literals_length != 0u) {
        result.literals = malloc(
            pattern->literals_length
        );

        if (result.literals == NULL) {
            matcher_nfa_free(&result);
            return MANGEN_MATCHER_OUT_OF_MEMORY;
        }

        memcpy(
            result.literals,
            pattern->literals,
            pattern->literals_length
        );

        result.literals_length = pattern->literals_length;
    }

    uint32_t current_state = 0u;
    uint32_t next_edge = 0u;

    result.edge_offsets[0] = 0u;

    for (size_t i = 0u; i < pattern->count; ++i) {
        const matcher_token_t *token = &pattern->tokens[i];

        matcher_nfa_edge_t edge = {0};

        switch (token->kind) {
        case MATCHER_TOKEN_LITERAL:
            edge.kind = MATCHER_NFA_LITERAL;
            edge.to = current_state + 1u;

            edge.literal_offset =
                (uint32_t)token->literal_offset;

            edge.literal_length =
                (uint32_t)token->literal_length;

            result.edges[next_edge++] = edge;

            ++current_state;
            result.edge_offsets[current_state] = next_edge;
            break;

        case MATCHER_TOKEN_ANY_ONE:
            edge.kind = MATCHER_NFA_ANY_ONE;
            edge.to = current_state + 1u;

            result.edges[next_edge++] = edge;

            ++current_state;
            result.edge_offsets[current_state] = next_edge;
            break;

        case MATCHER_TOKEN_ANY_MANY:
            edge.kind = MATCHER_NFA_ANY_MANY;
            edge.to = current_state;

            result.edges[next_edge++] = edge;
            break;

        default:
            matcher_nfa_free(&result);
            return MANGEN_MATCHER_INVALID_ARGUMENT;
        }
    }

    result.edge_offsets[result.state_count] = next_edge;

    *out = result;

    return MANGEN_MATCHER_OK;
}

mangen_matcher_status_t matcher_nfa_from_pattern(
    const char *source,
    matcher_nfa_t *out
)
{
    if (
        source == NULL ||
        out == NULL ||
        !matcher_nfa_is_empty(out)
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    matcher_pattern_t pattern = {0};

    mangen_matcher_status_t status =
        matcher_pattern_parse(
            source,
            &pattern
        );

    if (status != MANGEN_MATCHER_OK) {
        return status;
    }

    status = matcher_nfa_build(
        &pattern,
        out
    );

    matcher_pattern_free(&pattern);

    return status;
}

void matcher_nfa_free(matcher_nfa_t *nfa)
{
    if (nfa == NULL) {
        return;
    }

    free(nfa->edges);
    free(nfa->edge_offsets);
    free(nfa->literals);

    *nfa = (matcher_nfa_t){0};
}
