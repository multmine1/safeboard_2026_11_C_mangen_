#ifndef MANGEN_HASH_POOL_INTERNAL_H
#define MANGEN_HASH_POOL_INTERNAL_H

#include "mangen/hash_pool.h"
#include "mangen/queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

struct mangen_hash_pool {
    mangen_catalog_t *catalog;
    mangen_queue_t *queue;

    mangen_hash_algorithm_t algorithm;

    pthread_t *threads;
    size_t worker_count;
    size_t threads_started;

    size_t completed_count;
    size_t failed_count;

    pthread_mutex_t mutex;
    pthread_cond_t work_ready;

    bool started;
    bool closed;
    bool cancelled;
    bool joined;
};

void *mangen_hash_pool_worker(void *argument);

/*
 * Requires pool->mutex to be locked.
 */
void mangen_hash_pool_abort_locked(
    mangen_hash_pool_t *pool
);

#endif
