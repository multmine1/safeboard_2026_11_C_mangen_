#include "min_dfa.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t begin;
    uint32_t size;
} matcher_partition_block_t;

typedef struct {
    uint32_t *items;
    uint8_t *queued;

    uint32_t head;
    uint32_t tail;
    uint32_t capacity;
} matcher_worklist_t;

static void matcher_worklist_push(
    matcher_worklist_t *list,
    uint32_t block
)
{
    if (list->queued[block] != 0u) {
        return;
    }

    list->items[list->tail] = block;
    list->tail = (list->tail + 1u) % list->capacity;

    list->queued[block] = 1u;
}

static uint32_t matcher_worklist_pop(
    matcher_worklist_t *list
)
{
    const uint32_t block = list->items[list->head];

    list->head = (list->head + 1u) % list->capacity;
    list->queued[block] = 0u;

    return block;
}

static bool matcher_worklist_empty(
    const matcher_worklist_t *list
)
{
    return list->head == list->tail;
}

static mangen_matcher_status_t matcher_min_dfa_validate(
    const matcher_dfa_t *dfa
)
{
    if (
        dfa == NULL ||
        dfa->state_count == 0u ||
        dfa->state_count > MANGEN_MAX_DFA_STATES ||
        dfa->class_count == 0u ||
        dfa->class_count > MANGEN_MAX_PATTERN_BYTES + 1u ||
        dfa->start_state >= dfa->state_count ||
        dfa->transitions == NULL ||
        dfa->accepting == NULL ||
        dfa->classes == NULL ||
        (dfa->literals_length != 0u && dfa->literals == NULL)
    ) {
        return MANGEN_MATCHER_INVALID_ARGUMENT;
    }

    for (uint32_t s = 0u; s < dfa->state_count; ++s) {
        for (uint32_t c = 0u; c < dfa->class_count; ++c) {
            const uint32_t target = dfa->transitions[
                (size_t)s * dfa->class_count + c
            ];

            if (target >= dfa->state_count) {
                return MANGEN_MATCHER_INVALID_ARGUMENT;
            }
        }
    }

    for (uint32_t c = 1u; c < dfa->class_count; ++c) {
        const matcher_dfa_class_t *entry = &dfa->classes[c];

        if (
            entry->literal_length == 0u ||
            entry->literal_offset > dfa->literals_length ||
            entry->literal_length >
                dfa->literals_length - entry->literal_offset
        ) {
            return MANGEN_MATCHER_INVALID_ARGUMENT;
        }
    }

    return MANGEN_MATCHER_OK;
}

mangen_matcher_status_t matcher_min_dfa_build(
    const matcher_dfa_t *dfa,
    matcher_dfa_t *out
)
{
    if (
        out == NULL ||
        out == dfa ||
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
        matcher_min_dfa_validate(dfa);

    if (status != MANGEN_MATCHER_OK) {
        return status;
    }

    const uint32_t states = dfa->state_count;
    const uint32_t classes = dfa->class_count;

    const size_t buckets = (size_t)states * classes;

    matcher_partition_block_t *blocks = NULL;

    uint32_t *order = NULL;
    uint32_t *positions = NULL;
    uint32_t *state_block = NULL;

    uint32_t *inverse_offsets = NULL;
    matcher_dfa_state_id_t *inverse_sources = NULL;

    uint32_t *marked = NULL;
    uint32_t *affected = NULL;
    uint32_t *snapshot = NULL;

    uint32_t *work_items = NULL;
    uint8_t *work_flags = NULL;

    uint32_t *block_to_min = NULL;
    uint32_t *block_queue = NULL;

    matcher_dfa_t result = {0};

    blocks = calloc(states, sizeof(*blocks));
    order = malloc((size_t)states * sizeof(*order));
    positions = malloc((size_t)states * sizeof(*positions));
    state_block = malloc((size_t)states * sizeof(*state_block));

    marked = calloc(states, sizeof(*marked));
    affected = malloc((size_t)states * sizeof(*affected));
    snapshot = malloc((size_t)states * sizeof(*snapshot));

    work_items = malloc(
        ((size_t)states + 1u) * sizeof(*work_items)
    );

    work_flags = calloc(states, sizeof(*work_flags));

    block_to_min = malloc(
        (size_t)states * sizeof(*block_to_min)
    );

    block_queue = malloc(
        (size_t)states * sizeof(*block_queue)
    );

    if (
        blocks == NULL ||
        order == NULL ||
        positions == NULL ||
        state_block == NULL ||
        marked == NULL ||
        affected == NULL ||
        snapshot == NULL ||
        work_items == NULL ||
        work_flags == NULL ||
        block_to_min == NULL ||
        block_queue == NULL
    ) {
        status = MANGEN_MATCHER_OUT_OF_MEMORY;
        goto cleanup;
    }

    uint32_t accepting_count = 0u;

    for (uint32_t s = 0u; s < states; ++s) {
        if (dfa->accepting[s] != 0u) {
            ++accepting_count;
        }
    }

    uint32_t block_count = 1u;

    if (
        accepting_count == 0u ||
        accepting_count == states
    ) {
        blocks[0] = (matcher_partition_block_t){
            .begin = 0u,
            .size = states
        };

        for (uint32_t s = 0u; s < states; ++s) {
            order[s] = s;
            positions[s] = s;
            state_block[s] = 0u;
        }
    } else {
        blocks[0] = (matcher_partition_block_t){
            .begin = 0u,
            .size = accepting_count
        };

        blocks[1] = (matcher_partition_block_t){
            .begin = accepting_count,
            .size = states - accepting_count
        };

        block_count = 2u;

        uint32_t accepted_position = 0u;
        uint32_t rejected_position = accepting_count;

        for (uint32_t s = 0u; s < states; ++s) {
            const bool accepting = dfa->accepting[s] != 0u;

            const uint32_t position = accepting
                ? accepted_position++
                : rejected_position++;

            order[position] = s;
            positions[s] = position;
            state_block[s] = accepting ? 0u : 1u;
        }
    }

    if (block_count > 1u) {
        if (
            buckets > SIZE_MAX / sizeof(uint32_t) - 1u ||
            buckets > SIZE_MAX /
                sizeof(matcher_dfa_state_id_t)
        ) {
            status = MANGEN_MATCHER_LIMIT_EXCEEDED;
            goto cleanup;
        }

        inverse_offsets = calloc(
            buckets + 1u,
            sizeof(*inverse_offsets)
        );

        inverse_sources = malloc(
            buckets * sizeof(*inverse_sources)
        );

        if (
            inverse_offsets == NULL ||
            inverse_sources == NULL
        ) {
            status = MANGEN_MATCHER_OUT_OF_MEMORY;
            goto cleanup;
        }

        /*
         * Construct inverse transitions as CSR:
         *
         * bucket = target_state * class_count + class_id
         *
         * inverse_sources[
         *   inverse_offsets[bucket] :
         *   inverse_offsets[bucket + 1]
         * ]
         */

        for (uint32_t s = 0u; s < states; ++s) {
            for (uint32_t c = 0u; c < classes; ++c) {
                const uint32_t target = dfa->transitions[
                    (size_t)s * classes + c
                ];

                const size_t bucket =
                    (size_t)target * classes + c;

                ++inverse_offsets[bucket + 1u];
            }
        }

        for (size_t i = 1u; i <= buckets; ++i) {
            inverse_offsets[i] += inverse_offsets[i - 1u];
        }

        for (uint32_t s = 0u; s < states; ++s) {
            for (uint32_t c = 0u; c < classes; ++c) {
                const uint32_t target = dfa->transitions[
                    (size_t)s * classes + c
                ];

                const size_t bucket =
                    (size_t)target * classes + c;

                const uint32_t index =
                    --inverse_offsets[bucket + 1u];

                inverse_sources[index] =
                    (matcher_dfa_state_id_t)s;
            }
        }

        for (size_t i = 0u; i < buckets; ++i) {
            inverse_offsets[i] = inverse_offsets[i + 1u];
        }

        inverse_offsets[buckets] = (uint32_t)buckets;

        matcher_worklist_t worklist = {
            .items = work_items,
            .queued = work_flags,
            .head = 0u,
            .tail = 0u,
            .capacity = states + 1u
        };

        matcher_worklist_push(
            &worklist,
            blocks[0].size <= blocks[1].size ? 0u : 1u
        );

        while (!matcher_worklist_empty(&worklist)) {
            const uint32_t splitter =
                matcher_worklist_pop(&worklist);

            const uint32_t snapshot_count =
                blocks[splitter].size;

            memcpy(
                snapshot,
                order + blocks[splitter].begin,
                (size_t)snapshot_count * sizeof(*snapshot)
            );

            for (uint32_t c = 0u; c < classes; ++c) {
                uint32_t affected_count = 0u;

                /*
                 * X = { q | transition(q, c) belongs to splitter }
                 */

                for (
                    uint32_t i = 0u;
                    i < snapshot_count;
                    ++i
                ) {
                    const uint32_t target = snapshot[i];

                    const size_t bucket =
                        (size_t)target * classes + c;

                    const uint32_t begin =
                        inverse_offsets[bucket];

                    const uint32_t end =
                        inverse_offsets[bucket + 1u];

                    for (uint32_t j = begin; j < end; ++j) {
                        const uint32_t source =
                            inverse_sources[j];

                        const uint32_t block =
                            state_block[source];

                        if (marked[block] == 0u) {
                            affected[affected_count++] = block;
                        }

                        const uint32_t old_position =
                            positions[source];

                        const uint32_t front_position =
                            blocks[block].begin + marked[block];

                        const uint32_t displaced =
                            order[front_position];

                        order[front_position] = source;
                        order[old_position] = displaced;

                        positions[source] = front_position;
                        positions[displaced] = old_position;

                        ++marked[block];
                    }
                }

                for (
                    uint32_t i = 0u;
                    i < affected_count;
                    ++i
                ) {
                    const uint32_t block = affected[i];
                    const uint32_t selected = marked[block];

                    marked[block] = 0u;

                    if (
                        selected == 0u ||
                        selected == blocks[block].size
                    ) {
                        continue;
                    }

                    const uint32_t new_block = block_count++;

                    blocks[new_block] = (matcher_partition_block_t){
                        .begin = blocks[block].begin,
                        .size = selected
                    };

                    blocks[block].begin += selected;
                    blocks[block].size -= selected;

                    const uint32_t new_begin =
                        blocks[new_block].begin;

                    const uint32_t new_end =
                        new_begin + blocks[new_block].size;

                    for (
                        uint32_t j = new_begin;
                        j < new_end;
                        ++j
                    ) {
                        state_block[order[j]] = new_block;
                    }

                    if (worklist.queued[block] != 0u) {
                        matcher_worklist_push(
                            &worklist,
                            new_block
                        );
                    } else {
                        const uint32_t smaller =
                            blocks[new_block].size <=
                                blocks[block].size
                                ? new_block
                                : block;

                        matcher_worklist_push(
                            &worklist,
                            smaller
                        );
                    }
                }
            }
        }
    }

    /*
     * Build the quotient DFA.
     *
     * Assign new IDs by BFS from the initial block.
     * This also removes unreachable blocks.
     */

    for (uint32_t b = 0u; b < block_count; ++b) {
        block_to_min[b] = UINT32_MAX;
    }

    const uint32_t start_block =
        state_block[dfa->start_state];

    block_to_min[start_block] = 0u;
    block_queue[0] = start_block;

    uint32_t minimized_count = 1u;

    for (
        uint32_t i = 0u;
        i < minimized_count;
        ++i
    ) {
        const uint32_t block = block_queue[i];

        const uint32_t representative =
            order[blocks[block].begin];

        for (uint32_t c = 0u; c < classes; ++c) {
            const uint32_t target = dfa->transitions[
                (size_t)representative * classes + c
            ];

            const uint32_t target_block =
                state_block[target];

            if (block_to_min[target_block] != UINT32_MAX) {
                continue;
            }

            block_to_min[target_block] = minimized_count;
            block_queue[minimized_count++] = target_block;
        }
    }

    result.state_count = minimized_count;
    result.class_count = classes;
    result.start_state = 0u;

    result.transitions = malloc(
        (size_t)minimized_count *
            classes *
            sizeof(*result.transitions)
    );

    result.accepting = malloc(
        (size_t)minimized_count *
            sizeof(*result.accepting)
    );

    result.classes = malloc(
        (size_t)classes * sizeof(*result.classes)
    );

    if (
        result.transitions == NULL ||
        result.accepting == NULL ||
        result.classes == NULL
    ) {
        status = MANGEN_MATCHER_OUT_OF_MEMORY;
        goto cleanup;
    }

    memcpy(
        result.classes,
        dfa->classes,
        (size_t)classes * sizeof(*result.classes)
    );

    if (dfa->literals_length != 0u) {
        result.literals = malloc(dfa->literals_length);

        if (result.literals == NULL) {
            status = MANGEN_MATCHER_OUT_OF_MEMORY;
            goto cleanup;
        }

        memcpy(
            result.literals,
            dfa->literals,
            dfa->literals_length
        );

        result.literals_length = dfa->literals_length;
    }

    for (uint32_t s = 0u; s < minimized_count; ++s) {
        const uint32_t block = block_queue[s];

        const uint32_t representative =
            order[blocks[block].begin];

        result.accepting[s] =
            dfa->accepting[representative] != 0u ? 1u : 0u;

        for (uint32_t c = 0u; c < classes; ++c) {
            const uint32_t target = dfa->transitions[
                (size_t)representative * classes + c
            ];

            const uint32_t target_block =
                state_block[target];

            result.transitions[
                (size_t)s * classes + c
            ] = (matcher_dfa_state_id_t)
                block_to_min[target_block];
        }
    }

    *out = result;
    result = (matcher_dfa_t){0};

    status = MANGEN_MATCHER_OK;

cleanup:
    free(blocks);
    free(order);
    free(positions);
    free(state_block);

    free(inverse_offsets);
    free(inverse_sources);

    free(marked);
    free(affected);
    free(snapshot);

    free(work_items);
    free(work_flags);

    free(block_to_min);
    free(block_queue);

    matcher_dfa_free(&result);

    return status;
}
