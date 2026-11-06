#include "dfa.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if MANGEN_MAX_DFA_STATES > 65536u
#error "DFA state IDs require a wider integer type"
#endif

typedef struct {
    const unsigned char *data;
    uint32_t offset;
    uint32_t length;
} matcher_literal_ref_t;

typedef struct {
    uint64_t *subsets;
    uint32_t *slots;

    uint32_t count;
    uint32_t word_count;
    uint32_t slot_count;
} matcher_subset_table_t;

static int matcher_compare_bytes(
    const unsigned char *a,
    size_t a_length,
    const unsigned char *b,
    size_t b_length
)
{
    const size_t common =
        a_length < b_length ? a_length : b_length;

    const int result = memcmp(a, b, common);

    if (result != 0) {
        return result;
    }

    if (a_length < b_length) {
        return -1;
    }

    return a_length > b_length ? 1 : 0;
}

static int matcher_compare_literal_refs(
    const void *left,
    const void *right
)
{
    const matcher_literal_ref_t *a = left;
    const matcher_literal_ref_t *b = right;

    return matcher_compare_bytes(
        a->data, a->length,
        b->data, b->length
    );
}

static bool matcher_bit_test(
    const uint64_t *bits,
    uint32_t index
)
{
    return (
        bits[index / 64u] &
        (UINT64_C(1) << (index % 64u))
    ) != 0u;
}

static void matcher_bit_set(
    uint64_t *bits,
    uint32_t index
)
{
    bits[index / 64u] |=
        UINT64_C(1) << (index % 64u);
}

static uint64_t matcher_subset_hash(
    const uint64_t *bits,
    uint32_t word_count
)
{
    uint64_t hash = UINT64_C(14695981039346656037);

    for (uint32_t i = 0u; i < word_count; ++i) {
        hash ^= bits[i];
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static mangen_matcher_status_t matcher_subset_intern(
    matcher_subset_table_t *table,
    const uint64_t *bits,
    uint32_t *state,
    bool *inserted
)
{
    const uint32_t mask = table->slot_count - 1u;

    uint32_t slot =
        (uint32_t)matcher_subset_hash(
            bits,
            table->word_count
        ) & mask;

    for (;;) {
        const uint32_t existing = table->slots[slot];

        if (existing == 0u) {
            if (table->count >= MANGEN_MAX_DFA_STATES) {
                return MANGEN_MATCHER_LIMIT_EXCEEDED;
            }

            const uint32_t id = table->count++;

            memcpy(
                table->subsets +
                    (size_t)id * table->word_count,
                bits,
                (size_t)table->word_count * sizeof(uint64_t)
            );

            table->slots[slot] = id + 1u;

            *state = id;
            *inserted = true;

            return MANGEN_MATCHER_OK;
        }

        const uint32_t id = existing - 1u;

        if (
            memcmp(
                table->subsets +
                    (size_t)id * table->word_count,
                bits,
                (size_t)table->word_count * sizeof(uint64_t)
            ) == 0
        ) {
            *state = id;
            *inserted = false;

            return MANGEN_MATCHER_OK;
        }

        slot = (slot + 1u) & mask;
    }
}

static mangen_matcher_status_t matcher_dfa_validate_nfa(
    const matcher_nfa_t *nfa
)
{
    if (
        nfa == NULL ||
        nfa->state_count == 0u ||
        nfa->state_count > MANGEN_MAX_PATTERN_BYTES + 1u ||
        nfa->edge_count > MANGEN_MAX_PATTERN_BYTES ||
        nfa->edge_offsets == NULL ||
        (nfa->edge_count != 0u && nfa->edges == NULL) ||
        (nfa->literals_length != 0u && nfa->literals == NULL) ||
        nfa->literals_length > UINT32_MAX ||
        nfa->start_state != 0u ||
        nfa->accept_state != nfa->state_count - 1u
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    if (
        nfa->edge_offsets[0] != 0u ||
        nfa->edge_offsets[nfa->state_count] != nfa->edge_count
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    for (uint32_t s = 0u; s < nfa->state_count; ++s) {
        const uint32_t begin = nfa->edge_offsets[s];
        const uint32_t end = nfa->edge_offsets[s + 1u];

        if (begin > end || end > nfa->edge_count) {
            return MANGEN_MATCHER_INVALID_ARGUMENT;
        }

        for (uint32_t e = begin; e < end; ++e) {
            const matcher_nfa_edge_t *edge = &nfa->edges[e];

            if (edge->kind == MATCHER_NFA_ANY_MANY) {
                if (edge->to != s) {
                    return MANGEN_MATCHER_INVALID_ARGUMENT;
                }

                continue;
            }

            if (
                (edge->kind != MATCHER_NFA_LITERAL &&
                 edge->kind != MATCHER_NFA_ANY_ONE) ||
                s + 1u >= nfa->state_count ||
                edge->to != s + 1u
            ) {
                return MANGEN_MATCHER_INVALID_ARGUMENT;
            }

            if (
                edge->kind == MATCHER_NFA_LITERAL &&
                (
                    edge->literal_length == 0u ||
                    edge->literal_offset > nfa->literals_length ||
                    edge->literal_length >
                        nfa->literals_length - edge->literal_offset
                )
            ) {
                return MANGEN_MATCHER_INVALID_ARGUMENT;
            }
        }
    }

    return MANGEN_MATCHER_OK;
}

static mangen_matcher_status_t matcher_dfa_make_classes(
    const matcher_nfa_t *nfa,
    matcher_dfa_t *dfa
)
{
    matcher_literal_ref_t *refs = NULL;

    if (nfa->edge_count != 0u) {
        refs = malloc(
            (size_t)nfa->edge_count * sizeof(*refs)
        );

        if (refs == NULL) {
            return MANGEN_MATCHER_OUT_OF_MEMORY;
        }
    }

    uint32_t literal_count = 0u;

    for (uint32_t i = 0u; i < nfa->edge_count; ++i) {
        const matcher_nfa_edge_t *edge = &nfa->edges[i];

        if (edge->kind != MATCHER_NFA_LITERAL) {
            continue;
        }

        refs[literal_count++] = (matcher_literal_ref_t){
            .data = nfa->literals + edge->literal_offset,
            .offset = edge->literal_offset,
            .length = edge->literal_length
        };
    }

    if (literal_count != 0u) {
        qsort(
            refs,
            literal_count,
            sizeof(*refs),
            matcher_compare_literal_refs
        );
    }

    dfa->classes = calloc(
        (size_t)literal_count + 1u,
        sizeof(*dfa->classes)
    );

    if (dfa->classes == NULL) {
        free(refs);
        return MANGEN_MATCHER_OUT_OF_MEMORY;
    }

    dfa->class_count = 1u;

    for (uint32_t i = 0u; i < literal_count; ++i) {
        if (
            i != 0u &&
            matcher_compare_bytes(
                refs[i - 1u].data,
                refs[i - 1u].length,
                refs[i].data,
                refs[i].length
            ) == 0
        ) {
            continue;
        }

        dfa->classes[dfa->class_count++] =
            (matcher_dfa_class_t){
                .literal_offset = refs[i].offset,
                .literal_length = refs[i].length
            };
    }

    free(refs);

    if (nfa->literals_length != 0u) {
        dfa->literals = malloc(nfa->literals_length);

        if (dfa->literals == NULL) {
            return MANGEN_MATCHER_OUT_OF_MEMORY;
        }

        memcpy(
            dfa->literals,
            nfa->literals,
            nfa->literals_length
        );

        dfa->literals_length = nfa->literals_length;
    }

    return MANGEN_MATCHER_OK;
}

uint32_t matcher_dfa_class_of(
    const matcher_dfa_t *dfa,
    const unsigned char *grapheme,
    size_t length
)
{
    if (
        dfa == NULL ||
        dfa->classes == NULL ||
        grapheme == NULL ||
        length == 0u
    ) {
        return 0u;
    }

    uint32_t low = 1u;
    uint32_t high = dfa->class_count;

    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;

        const matcher_dfa_class_t *entry =
            &dfa->classes[middle];

        const int comparison = matcher_compare_bytes(
            grapheme,
            length,
            dfa->literals + entry->literal_offset,
            entry->literal_length
        );

        if (comparison == 0) {
            return middle;
        }

        if (comparison < 0) {
            high = middle;
        } else {
            low = middle + 1u;
        }
    }

    return 0u;
}

static void matcher_dfa_common_transition(
    const uint64_t *subset,
    const uint64_t *star_mask,
    const uint64_t *dot_mask,
    uint64_t *result,
    uint32_t word_count,
    uint32_t state_count
)
{
    uint64_t carry = 0u;

    for (uint32_t w = 0u; w < word_count; ++w) {
        const uint64_t advance = subset[w] & dot_mask[w];

        result[w] =
            (subset[w] & star_mask[w]) |
            (advance << 1u) |
            carry;

        carry = advance >> 63u;
    }

    const uint32_t remainder = state_count % 64u;

    if (remainder != 0u) {
        result[word_count - 1u] &=
            (UINT64_C(1) << remainder) - 1u;
    }
}

static void matcher_dfa_literal_transition(
    const uint64_t *subset,
    const uint64_t *common,
    const uint64_t *literal_mask,
    uint64_t *result,
    uint32_t word_count,
    uint32_t state_count
)
{
    uint64_t carry = 0u;

    for (uint32_t w = 0u; w < word_count; ++w) {
        const uint64_t advance =
            subset[w] & literal_mask[w];

        result[w] =
            common[w] |
            (advance << 1u) |
            carry;

        carry = advance >> 63u;
    }

    const uint32_t remainder = state_count % 64u;

    if (remainder != 0u) {
        result[word_count - 1u] &=
            (UINT64_C(1) << remainder) - 1u;
    }
}

static mangen_matcher_status_t matcher_dfa_ensure_rows(
    matcher_dfa_t *dfa,
    uint32_t *capacity,
    uint32_t required
)
{
    if (required <= *capacity) {
        return MANGEN_MATCHER_OK;
    }

    uint32_t next = *capacity != 0u ? *capacity : 8u;

    while (next < required) {
        if (next >= MANGEN_MAX_DFA_STATES / 2u) {
            next = MANGEN_MAX_DFA_STATES;
            break;
        }

        next *= 2u;
    }

    if (
        (size_t)next >
        SIZE_MAX / sizeof(*dfa->transitions) / dfa->class_count
    ) {
        return MANGEN_MATCHER_LIMIT_EXCEEDED;
    }

    matcher_dfa_state_id_t *resized = realloc(
        dfa->transitions,
        (size_t)next *
            dfa->class_count *
            sizeof(*dfa->transitions)
    );

    if (resized == NULL) {
        return MANGEN_MATCHER_OUT_OF_MEMORY;
    }

    dfa->transitions = resized;
    *capacity = next;

    return MANGEN_MATCHER_OK;
}

static mangen_matcher_status_t matcher_dfa_add_subset(
    matcher_subset_table_t *table,
    matcher_dfa_t *dfa,
    const matcher_nfa_t *nfa,
    const uint64_t *bits,
    uint32_t *state
)
{
    bool inserted = false;

    const mangen_matcher_status_t status =
        matcher_subset_intern(
            table,
            bits,
            state,
            &inserted
        );

    if (status != MANGEN_MATCHER_OK) {
        return status;
    }

    if (inserted) {
        dfa->accepting[*state] =
            matcher_bit_test(
                bits,
                nfa->accept_state
            ) ? 1u : 0u;
    }

    return MANGEN_MATCHER_OK;
}

mangen_matcher_status_t matcher_dfa_build(
    const matcher_nfa_t *nfa,
    matcher_dfa_t *out
)
{
    if (
        out == NULL ||
        out->transitions != NULL ||
        out->accepting != NULL ||
        out->classes != NULL ||
        out->literals != NULL ||
        out->state_count != 0u ||
        out->class_count != 0u ||
        out->start_state != 0u ||
        out->literals_length != 0u
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    mangen_matcher_status_t status =
        matcher_dfa_validate_nfa(nfa);

    if (status != MANGEN_MATCHER_OK) {
        return status;
    }

    matcher_dfa_t dfa = {0};
    matcher_subset_table_t table = {0};

    uint64_t *star_mask = NULL;
    uint64_t *dot_mask = NULL;
    uint64_t *literal_masks = NULL;
    uint64_t *common = NULL;
    uint64_t *next = NULL;
    uint64_t *pending = NULL;

    uint32_t *literal_classes = NULL;

    status = matcher_dfa_make_classes(nfa, &dfa);

    if (status != MANGEN_MATCHER_OK) {
        goto cleanup;
    }

    const uint32_t word_count =
        (nfa->state_count + 63u) / 64u;

    const uint32_t literal_count = dfa.class_count - 1u;

    const uint32_t pending_word_count =
        (literal_count + 63u) / 64u;

    star_mask = calloc(word_count, sizeof(uint64_t));
    dot_mask = calloc(word_count, sizeof(uint64_t));
    common = calloc(word_count, sizeof(uint64_t));
    next = calloc(word_count, sizeof(uint64_t));

    literal_masks = calloc(
        (size_t)dfa.class_count * word_count,
        sizeof(uint64_t)
    );

    literal_classes = calloc(
        nfa->state_count,
        sizeof(uint32_t)
    );

    if (pending_word_count != 0u) {
        pending = calloc(
            pending_word_count,
            sizeof(uint64_t)
        );
    }

    if (
        star_mask == NULL ||
        dot_mask == NULL ||
        common == NULL ||
        next == NULL ||
        literal_masks == NULL ||
        literal_classes == NULL ||
        (pending_word_count != 0u && pending == NULL)
    ) {
        status = MANGEN_MATCHER_OUT_OF_MEMORY;
        goto cleanup;
    }

    for (uint32_t s = 0u; s < nfa->state_count; ++s) {
        const uint32_t begin = nfa->edge_offsets[s];
        const uint32_t end = nfa->edge_offsets[s + 1u];

        for (uint32_t e = begin; e < end; ++e) {
            const matcher_nfa_edge_t *edge = &nfa->edges[e];

            switch (edge->kind) {
            case MATCHER_NFA_ANY_MANY:
                matcher_bit_set(star_mask, s);
                break;

            case MATCHER_NFA_ANY_ONE:
                matcher_bit_set(dot_mask, s);
                break;

            case MATCHER_NFA_LITERAL: {
                if (literal_classes[s] != 0u) {
                    status = MANGEN_MATCHER_INVALID_ARGUMENT;
                    goto cleanup;
                }

                const uint32_t class_id =
                    matcher_dfa_class_of(
                        &dfa,
                        nfa->literals + edge->literal_offset,
                        edge->literal_length
                    );

                if (class_id == 0u) {
                    status = MANGEN_MATCHER_INVALID_ARGUMENT;
                    goto cleanup;
                }

                literal_classes[s] = class_id;

                matcher_bit_set(
                    literal_masks +
                        (size_t)class_id * word_count,
                    s
                );

                break;
            }

            default:
                status = MANGEN_MATCHER_INVALID_ARGUMENT;
                goto cleanup;
            }
        }
    }

    table.word_count = word_count;

    table.slot_count = 1u;

    while (
        table.slot_count <
        MANGEN_MAX_DFA_STATES * 2u
    ) {
        table.slot_count *= 2u;
    }

    table.subsets = calloc(
        (size_t)MANGEN_MAX_DFA_STATES * word_count,
        sizeof(uint64_t)
    );

    table.slots = calloc(
        table.slot_count,
        sizeof(uint32_t)
    );

    dfa.accepting = calloc(
        MANGEN_MAX_DFA_STATES,
        sizeof(uint8_t)
    );

    if (
        table.subsets == NULL ||
        table.slots == NULL ||
        dfa.accepting == NULL
    ) {
        status = MANGEN_MATCHER_OUT_OF_MEMORY;
        goto cleanup;
    }

    matcher_bit_set(next, nfa->start_state);

    uint32_t initial_state = 0u;

    status = matcher_dfa_add_subset(
        &table,
        &dfa,
        nfa,
        next,
        &initial_state
    );

    if (status != MANGEN_MATCHER_OK) {
        goto cleanup;
    }

    dfa.start_state = (matcher_dfa_state_id_t)initial_state;

    uint32_t row_capacity = 0u;

    for (uint32_t state = 0u; state < table.count; ++state) {
        status = matcher_dfa_ensure_rows(
            &dfa,
            &row_capacity,
            state + 1u
        );

        if (status != MANGEN_MATCHER_OK) {
            goto cleanup;
        }

        const uint64_t *subset =
            table.subsets +
            (size_t)state * word_count;

        matcher_dfa_common_transition(
            subset,
            star_mask,
            dot_mask,
            common,
            word_count,
            nfa->state_count
        );

        uint32_t default_state = 0u;

        status = matcher_dfa_add_subset(
            &table,
            &dfa,
            nfa,
            common,
            &default_state
        );

        if (status != MANGEN_MATCHER_OK) {
            goto cleanup;
        }

        const size_t row =
            (size_t)state * dfa.class_count;

        for (uint32_t c = 0u; c < dfa.class_count; ++c) {
            dfa.transitions[row + c] =
                (matcher_dfa_state_id_t)default_state;
        }

        if (literal_count == 0u) {
            continue;
        }

        memset(
            pending,
            0,
            (size_t)pending_word_count * sizeof(uint64_t)
        );

        for (uint32_t s = 0u; s < nfa->state_count; ++s) {
            if (!matcher_bit_test(subset, s)) {
                continue;
            }

            const uint32_t class_id = literal_classes[s];

            if (class_id == 0u) {
                continue;
            }

            const uint32_t bit = class_id - 1u;

            pending[bit / 64u] |=
                UINT64_C(1) << (bit % 64u);
        }

        for (uint32_t w = 0u; w < pending_word_count; ++w) {
            uint64_t bits = pending[w];

            while (bits != 0u) {
                unsigned bit = 0u;

                while (
                    ((bits >> bit) & UINT64_C(1)) == 0u
                ) {
                    ++bit;
                }

                bits &= bits - 1u;

                const uint32_t class_id =
                    w * 64u + bit + 1u;

                matcher_dfa_literal_transition(
                    subset,
                    common,
                    literal_masks +
                        (size_t)class_id * word_count,
                    next,
                    word_count,
                    nfa->state_count
                );

                uint32_t target = 0u;

                status = matcher_dfa_add_subset(
                    &table,
                    &dfa,
                    nfa,
                    next,
                    &target
                );

                if (status != MANGEN_MATCHER_OK) {
                    goto cleanup;
                }

                dfa.transitions[row + class_id] =
                    (matcher_dfa_state_id_t)target;
            }
        }
    }

    dfa.state_count = table.count;

    *out = dfa;
    dfa = (matcher_dfa_t){0};

    status = MANGEN_MATCHER_OK;

cleanup:
    free(star_mask);
    free(dot_mask);
    free(literal_masks);
    free(common);
    free(next);
    free(pending);
    free(literal_classes);

    free(table.subsets);
    free(table.slots);

    matcher_dfa_free(&dfa);

    return status;
}

bool matcher_dfa_is_accepting(
    const matcher_dfa_t *dfa,
    uint32_t state
)
{
    return
        dfa != NULL &&
        dfa->accepting != NULL &&
        state < dfa->state_count &&
        dfa->accepting[state] != 0u;
}

void matcher_dfa_free(matcher_dfa_t *dfa)
{
    if (dfa == NULL) {
        return;
    }

    free(dfa->transitions);
    free(dfa->accepting);
    free(dfa->classes);
    free(dfa->literals);

    *dfa = (matcher_dfa_t){0};
}
