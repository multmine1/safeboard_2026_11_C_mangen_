#ifndef MANGEN_QUEUE_INTERNAL_H
#define MANGEN_QUEUE_INTERNAL_H

#include "mangen/queue.h"

#include <ck_ring.h>

#include <stdatomic.h>
#include <stddef.h>

typedef enum {
    MANGEN_QUEUE_STATE_OPEN = 0,
    MANGEN_QUEUE_STATE_CLOSED,
    MANGEN_QUEUE_STATE_ABORTED
} mangen_queue_state_t;

struct mangen_queue {
    ck_ring_t ring;
    ck_ring_buffer_t *buffer;

    size_t capacity;

    atomic_uint state;

    mangen_queue_dispose_fn dispose;
    void *dispose_context;
};

#endif
