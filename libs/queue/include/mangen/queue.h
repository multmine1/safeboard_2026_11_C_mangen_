#ifndef MANGEN_QUEUE_H
#define MANGEN_QUEUE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mangen_queue mangen_queue_t;

typedef enum {
    MANGEN_QUEUE_OK = 0,
    MANGEN_QUEUE_EMPTY,
    MANGEN_QUEUE_FULL,
    MANGEN_QUEUE_CLOSED,
    MANGEN_QUEUE_ABORTED,
    MANGEN_QUEUE_INVALID_ARGUMENT,
    MANGEN_QUEUE_OUT_OF_MEMORY,
    MANGEN_QUEUE_LIMIT_EXCEEDED
} mangen_queue_status_t;

/*
 * Called when a queued item must be discarded during destruction.
 *
 * item:
 *   Pointer previously accepted by the queue.
 *
 * context:
 *   User-provided context from mangen_queue_create().
 *
 * A NULL callback is permitted.
 */
typedef void (*mangen_queue_dispose_fn)(
    void *item,
    void *context
);

/*
 * Creates a fixed-capacity MPMC queue.
 *
 * requested_capacity:
 *   Minimum number of items the queue must accommodate.
 *   Must be greater than zero.
 *
 * The actual capacity may be rounded upward to satisfy
 * Concurrency Kit ring-buffer requirements.
 *
 * dispose:
 *   Optional callback for unconsumed items.
 *
 * context:
 *   Context passed to dispose().
 *
 * out:
 *   Receives an owned queue on success.
 *   Set to NULL on failure.
 *
 * The capacity remains unchanged throughout the queue lifetime.
 */
mangen_queue_status_t mangen_queue_create(
    size_t requested_capacity,
    mangen_queue_dispose_fn dispose,
    void *context,
    mangen_queue_t **out
);

/*
 * Attempts to enqueue one non-NULL item.
 *
 * MANGEN_QUEUE_OK:
 *   Item accepted; ownership transfers to the queue.
 *
 * MANGEN_QUEUE_FULL:
 *   No capacity available; caller retains ownership.
 *
 * MANGEN_QUEUE_CLOSED:
 *   Queue does not accept new items.
 *
 * MANGEN_QUEUE_ABORTED:
 *   Processing has been cancelled.
 *
 * Other failures leave ownership with the caller.
 *
 * Does not allocate memory or resize the queue.
 * Safe for concurrent producers.
 */
mangen_queue_status_t mangen_queue_try_push(
    mangen_queue_t *queue,
    void *item
);

/*
 * Attempts to dequeue one item.
 *
 * MANGEN_QUEUE_OK:
 *   *out receives the item.
 *   Ownership transfers to the caller.
 *
 * MANGEN_QUEUE_EMPTY:
 *   Queue is currently empty but remains open.
 *
 * MANGEN_QUEUE_CLOSED:
 *   Queue is closed and drained.
 *
 * MANGEN_QUEUE_ABORTED:
 *   Processing has been cancelled.
 *
 * *out is set to NULL on every non-OK result,
 * provided out itself is non-NULL.
 *
 * Safe for concurrent consumers.
 */
mangen_queue_status_t mangen_queue_try_pop(
    mangen_queue_t *queue,
    void **out
);

/*
 * Stops accepting new items.
 *
 * Must be called after every producer has finished
 * its final enqueue operation.
 *
 * Consumers may continue draining existing items.
 *
 * Repeated close operations are permitted.
 */
mangen_queue_status_t mangen_queue_close(
    mangen_queue_t *queue
);

/*
 * Requests cancellation of all queue processing.
 *
 * May be called concurrently with producers and consumers.
 * Operations already in progress may still complete.
 *
 * The caller must stop and join all worker threads
 * before destroying the queue.
 *
 * Repeated abort operations are permitted.
 */
void mangen_queue_abort(
    mangen_queue_t *queue
);

/*
 * Returns the actual usable capacity.
 *
 * The value is constant for the entire queue lifetime.
 * Returns zero for NULL.
 */
size_t mangen_queue_capacity(
    const mangen_queue_t *queue
);

/*
 * Destroys the queue.
 *
 * All producers and consumers must have terminated.
 *
 * Remaining items are passed to dispose(), if provided.
 * The queue buffer and the queue object are freed.
 *
 * Accepts NULL.
 */
void mangen_queue_destroy(
    mangen_queue_t *queue
);

#ifdef __cplusplus
}
#endif

#endif
