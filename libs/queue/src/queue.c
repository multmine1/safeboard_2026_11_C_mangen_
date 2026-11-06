#include "queue_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static mangen_queue_status_t queue_calculate_ring_size(
    size_t requested_capacity,
    unsigned int *ring_size
) {
    if (requested_capacity == 0 || ring_size == NULL) {
        return MANGEN_QUEUE_INVALID_ARGUMENT;
    }

    unsigned int size = 4;

    while ((size_t)(size - 1) < requested_capacity) {
        if (size > UINT_MAX / 2) {
            return MANGEN_QUEUE_LIMIT_EXCEEDED;
        }

        size *= 2;
    }

    if ((size_t)size > SIZE_MAX / sizeof(ck_ring_buffer_t)) {
        return MANGEN_QUEUE_LIMIT_EXCEEDED;
    }

    *ring_size = size;

    return MANGEN_QUEUE_OK;
}

mangen_queue_status_t mangen_queue_create(
    size_t requested_capacity,
    mangen_queue_dispose_fn dispose,
    void *context,
    mangen_queue_t **out
) {
    if (out == NULL) {
        return MANGEN_QUEUE_INVALID_ARGUMENT;
    }

    *out = NULL;

    unsigned int ring_size = 0;

    mangen_queue_status_t status = queue_calculate_ring_size(
        requested_capacity,
        &ring_size
    );

    if (status != MANGEN_QUEUE_OK) {
        return status;
    }

    mangen_queue_t *queue = calloc(1, sizeof(*queue));

    if (queue == NULL) {
        return MANGEN_QUEUE_OUT_OF_MEMORY;
    }

    queue->buffer = calloc(
        ring_size,
        sizeof(*queue->buffer)
    );

    if (queue->buffer == NULL) {
        free(queue);
        return MANGEN_QUEUE_OUT_OF_MEMORY;
    }

    ck_ring_init(&queue->ring, ring_size);

    queue->capacity = (size_t)ring_size - 1;
    queue->dispose = dispose;
    queue->dispose_context = context;

    atomic_init(
        &queue->state,
        MANGEN_QUEUE_STATE_OPEN
    );

    *out = queue;

    return MANGEN_QUEUE_OK;
}

mangen_queue_status_t mangen_queue_try_push(
    mangen_queue_t *queue,
    void *item
) {
    if (queue == NULL || item == NULL) {
        return MANGEN_QUEUE_INVALID_ARGUMENT;
    }

    unsigned int state = atomic_load_explicit(
        &queue->state,
        memory_order_acquire
    );

    if (state == MANGEN_QUEUE_STATE_ABORTED) {
        return MANGEN_QUEUE_ABORTED;
    }

    if (state == MANGEN_QUEUE_STATE_CLOSED) {
        return MANGEN_QUEUE_CLOSED;
    }

    if (ck_ring_enqueue_mpmc(
            &queue->ring,
            queue->buffer,
            item
        )) {
        return MANGEN_QUEUE_OK;
    }

    state = atomic_load_explicit(
        &queue->state,
        memory_order_acquire
    );

    if (state == MANGEN_QUEUE_STATE_ABORTED) {
        return MANGEN_QUEUE_ABORTED;
    }

    if (state == MANGEN_QUEUE_STATE_CLOSED) {
        return MANGEN_QUEUE_CLOSED;
    }

    return MANGEN_QUEUE_FULL;
}

mangen_queue_status_t mangen_queue_try_pop(
    mangen_queue_t *queue,
    void **out
) {
    if (out == NULL) {
        return MANGEN_QUEUE_INVALID_ARGUMENT;
    }

    *out = NULL;

    if (queue == NULL) {
        return MANGEN_QUEUE_INVALID_ARGUMENT;
    }

    unsigned int state = atomic_load_explicit(
        &queue->state,
        memory_order_acquire
    );

    if (state == MANGEN_QUEUE_STATE_ABORTED) {
        return MANGEN_QUEUE_ABORTED;
    }

    void *item = NULL;

    if (ck_ring_trydequeue_mpmc(
            &queue->ring,
            queue->buffer,
            &item
        )) {
        *out = item;
        return MANGEN_QUEUE_OK;
    }

    state = atomic_load_explicit(
        &queue->state,
        memory_order_acquire
    );

    if (state == MANGEN_QUEUE_STATE_ABORTED) {
        return MANGEN_QUEUE_ABORTED;
    }

    if (state == MANGEN_QUEUE_STATE_CLOSED) {
        return MANGEN_QUEUE_CLOSED;
    }

    return MANGEN_QUEUE_EMPTY;
}

size_t mangen_queue_capacity(
    const mangen_queue_t *queue
) {
    return queue != NULL ? queue->capacity : 0;
}

void mangen_queue_destroy(
    mangen_queue_t *queue
) {
    if (queue == NULL) {
        return;
    }

    void *item = NULL;

    while (ck_ring_dequeue_mpmc(
        &queue->ring,
        queue->buffer,
        &item
    )) {
        if (queue->dispose != NULL) {
            queue->dispose(
                item,
                queue->dispose_context
            );
        }
    }

    free(queue->buffer);
    free(queue);
}
