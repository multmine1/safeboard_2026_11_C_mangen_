#include "queue_internal.h"

#include <stdatomic.h>

mangen_queue_status_t mangen_queue_close(
    mangen_queue_t *queue
) {
    if (queue == NULL) {
        return MANGEN_QUEUE_INVALID_ARGUMENT;
    }

    unsigned int expected = MANGEN_QUEUE_STATE_OPEN;

    if (atomic_compare_exchange_strong_explicit(
            &queue->state,
            &expected,
            MANGEN_QUEUE_STATE_CLOSED,
            memory_order_acq_rel,
            memory_order_acquire
        )) {
        return MANGEN_QUEUE_OK;
    }

    if (expected == MANGEN_QUEUE_STATE_CLOSED) {
        return MANGEN_QUEUE_OK;
    }

    return MANGEN_QUEUE_ABORTED;
}

void mangen_queue_abort(
    mangen_queue_t *queue
) {
    if (queue == NULL) {
        return;
    }

    atomic_store_explicit(
        &queue->state,
        MANGEN_QUEUE_STATE_ABORTED,
        memory_order_release
    );
}
