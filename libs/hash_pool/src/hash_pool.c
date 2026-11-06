#include "hash_pool_internal.h"

#include <stdint.h>
#include <stdlib.h>

static mangen_hash_pool_status_t map_queue_status(
    mangen_queue_status_t status
) {
    switch (status) {
        case MANGEN_QUEUE_OK:
            return MANGEN_HASH_POOL_OK;

        case MANGEN_QUEUE_INVALID_ARGUMENT:
            return MANGEN_HASH_POOL_INVALID_ARGUMENT;

        case MANGEN_QUEUE_OUT_OF_MEMORY:
            return MANGEN_HASH_POOL_OUT_OF_MEMORY;

        case MANGEN_QUEUE_LIMIT_EXCEEDED:
            return MANGEN_HASH_POOL_LIMIT_EXCEEDED;

        case MANGEN_QUEUE_FULL:
            return MANGEN_HASH_POOL_QUEUE_FULL;

        case MANGEN_QUEUE_ABORTED:
            return MANGEN_HASH_POOL_CANCELLED;

        default:
            return MANGEN_HASH_POOL_INVALID_STATE;
    }
}

void mangen_hash_pool_abort_locked(
    mangen_hash_pool_t *pool
) {
    if (pool->cancelled) {
        return;
    }

    pool->cancelled = true;
    pool->closed = true;

    mangen_queue_abort(pool->queue);
    mangen_catalog_cancel(pool->catalog);

    pthread_cond_broadcast(&pool->work_ready);
}

mangen_hash_pool_status_t mangen_hash_pool_create(
    mangen_catalog_t *catalog,
    mangen_hash_algorithm_t algorithm,
    size_t worker_count,
    size_t queue_capacity,
    mangen_hash_pool_t **out
) {
    if (out == NULL) {
        return MANGEN_HASH_POOL_INVALID_ARGUMENT;
    }

    *out = NULL;

    if (catalog == NULL ||
        worker_count == 0u ||
        queue_capacity == 0u ||
        mangen_hash_digest_size(algorithm) == 0u) {
        return MANGEN_HASH_POOL_INVALID_ARGUMENT;
    }

    if (worker_count > SIZE_MAX / sizeof(pthread_t)) {
        return MANGEN_HASH_POOL_LIMIT_EXCEEDED;
    }

    mangen_hash_pool_t *pool = calloc(1u, sizeof(*pool));

    if (pool == NULL) {
        return MANGEN_HASH_POOL_OUT_OF_MEMORY;
    }

    pool->catalog = catalog;
    pool->algorithm = algorithm;
    pool->worker_count = worker_count;

    mangen_queue_status_t queue_status =
        mangen_queue_create(
            queue_capacity,
            NULL,
            NULL,
            &pool->queue
        );

    if (queue_status != MANGEN_QUEUE_OK) {
        free(pool);
        return map_queue_status(queue_status);
    }

    pool->threads = calloc(
        worker_count,
        sizeof(*pool->threads)
    );

    if (pool->threads == NULL) {
        mangen_queue_destroy(pool->queue);
        free(pool);
        return MANGEN_HASH_POOL_OUT_OF_MEMORY;
    }

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool->threads);
        mangen_queue_destroy(pool->queue);
        free(pool);
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    if (pthread_cond_init(&pool->work_ready, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool->threads);
        mangen_queue_destroy(pool->queue);
        free(pool);
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    *out = pool;

    return MANGEN_HASH_POOL_OK;
}

mangen_hash_pool_status_t mangen_hash_pool_start(
    mangen_hash_pool_t *pool
) {
    if (pool == NULL) {
        return MANGEN_HASH_POOL_INVALID_ARGUMENT;
    }

    if (pthread_mutex_lock(&pool->mutex) != 0) {
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    if (pool->cancelled) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_CANCELLED;
    }

    if (pool->started || pool->closed || pool->joined) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_INVALID_STATE;
    }

    pool->started = true;

    for (size_t i = 0u; i < pool->worker_count; ++i) {
        const int rc = pthread_create(
            &pool->threads[i],
            NULL,
            mangen_hash_pool_worker,
            pool
        );

        if (rc != 0) {
            mangen_hash_pool_abort_locked(pool);

            const size_t created = pool->threads_started;

            pthread_mutex_unlock(&pool->mutex);

            for (size_t j = 0u; j < created; ++j) {
                pthread_join(pool->threads[j], NULL);
            }

            pthread_mutex_lock(&pool->mutex);
            pool->joined = true;
            pthread_mutex_unlock(&pool->mutex);

            return MANGEN_HASH_POOL_SYSTEM_ERROR;
        }

        ++pool->threads_started;
    }

    pthread_mutex_unlock(&pool->mutex);

    return MANGEN_HASH_POOL_OK;
}

mangen_hash_pool_status_t mangen_hash_pool_submit(
    mangen_hash_pool_t *pool,
    size_t catalog_index
) {
    if (pool == NULL) {
        return MANGEN_HASH_POOL_INVALID_ARGUMENT;
    }

    mangen_catalog_entry_t *entry =
        mangen_catalog_entry_at(
            pool->catalog,
            catalog_index
        );

    if (entry == NULL) {
        return MANGEN_HASH_POOL_INVALID_ARGUMENT;
    }

    if (pthread_mutex_lock(&pool->mutex) != 0) {
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    if (pool->cancelled) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_CANCELLED;
    }

    if (!pool->started || pool->closed || pool->joined) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_INVALID_STATE;
    }

    mangen_catalog_result_t current;

    const mangen_catalog_status_t catalog_status =
        mangen_catalog_get_result(entry, &current);

    if (catalog_status == MANGEN_CATALOG_CANCELLED) {
        mangen_hash_pool_abort_locked(pool);
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_CANCELLED;
    }

    if (catalog_status != MANGEN_CATALOG_OK ||
        current.state != MANGEN_CATALOG_PENDING) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_INVALID_STATE;
    }

    const mangen_queue_status_t queue_status =
        mangen_queue_try_push(pool->queue, entry);

    if (queue_status == MANGEN_QUEUE_FULL) {
        mangen_hash_pool_abort_locked(pool);
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_QUEUE_FULL;
    }

    if (queue_status != MANGEN_QUEUE_OK) {
        const mangen_hash_pool_status_t result =
            map_queue_status(queue_status);

        pthread_mutex_unlock(&pool->mutex);
        return result;
    }

    /*
     * Workers also acquire pool->mutex before popping.
     * The entry cannot be processed before this transition.
     */
    if (mangen_catalog_mark_processing(entry) !=
        MANGEN_CATALOG_OK) {
        mangen_hash_pool_abort_locked(pool);
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_INVALID_STATE;
    }

    pthread_cond_signal(&pool->work_ready);
    pthread_mutex_unlock(&pool->mutex);

    return MANGEN_HASH_POOL_OK;
}

mangen_hash_pool_status_t mangen_hash_pool_close(
    mangen_hash_pool_t *pool
) {
    if (pool == NULL) {
        return MANGEN_HASH_POOL_INVALID_ARGUMENT;
    }

    if (pthread_mutex_lock(&pool->mutex) != 0) {
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    if (pool->cancelled) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_CANCELLED;
    }

    if (!pool->started || pool->joined) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_INVALID_STATE;
    }

    if (pool->closed) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_OK;
    }

    const mangen_queue_status_t status =
        mangen_queue_close(pool->queue);

    if (status != MANGEN_QUEUE_OK) {
        pthread_mutex_unlock(&pool->mutex);
        return map_queue_status(status);
    }

    pool->closed = true;

    pthread_cond_broadcast(&pool->work_ready);
    pthread_mutex_unlock(&pool->mutex);

    return MANGEN_HASH_POOL_OK;
}

mangen_hash_pool_status_t mangen_hash_pool_join(
    mangen_hash_pool_t *pool
) {
    if (pool == NULL) {
        return MANGEN_HASH_POOL_INVALID_ARGUMENT;
    }

    if (pthread_mutex_lock(&pool->mutex) != 0) {
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    if (!pool->started) {
        pthread_mutex_unlock(&pool->mutex);
        return MANGEN_HASH_POOL_INVALID_STATE;
    }

    if (pool->joined) {
        const bool cancelled = pool->cancelled;

        pthread_mutex_unlock(&pool->mutex);

        return cancelled
            ? MANGEN_HASH_POOL_CANCELLED
            : MANGEN_HASH_POOL_OK;
    }

    if (!pool->closed && !pool->cancelled) {
        const mangen_queue_status_t status =
            mangen_queue_close(pool->queue);

        if (status != MANGEN_QUEUE_OK) {
            mangen_hash_pool_abort_locked(pool);
        } else {
            pool->closed = true;
            pthread_cond_broadcast(&pool->work_ready);
        }
    }

    const size_t count = pool->threads_started;

    pthread_mutex_unlock(&pool->mutex);

    bool join_failed = false;

    for (size_t i = 0u; i < count; ++i) {
        if (pthread_join(pool->threads[i], NULL) != 0) {
            join_failed = true;
        }
    }

    if (pthread_mutex_lock(&pool->mutex) != 0) {
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    pool->joined = !join_failed;

    const bool cancelled = pool->cancelled;

    pthread_mutex_unlock(&pool->mutex);

    if (join_failed) {
        return MANGEN_HASH_POOL_SYSTEM_ERROR;
    }

    return cancelled
        ? MANGEN_HASH_POOL_CANCELLED
        : MANGEN_HASH_POOL_OK;
}

void mangen_hash_pool_cancel(
    mangen_hash_pool_t *pool
) {
    if (pool == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);

    mangen_hash_pool_abort_locked(pool);

    pthread_mutex_unlock(&pool->mutex);
}

size_t mangen_hash_pool_completed(
    mangen_hash_pool_t *pool
) {
    if (pool == NULL) {
        return 0u;
    }

    pthread_mutex_lock(&pool->mutex);

    const size_t result = pool->completed_count;

    pthread_mutex_unlock(&pool->mutex);

    return result;
}

size_t mangen_hash_pool_failed(
    mangen_hash_pool_t *pool
) {
    if (pool == NULL) {
        return 0u;
    }

    pthread_mutex_lock(&pool->mutex);

    const size_t result = pool->failed_count;

    pthread_mutex_unlock(&pool->mutex);

    return result;
}

void mangen_hash_pool_destroy(
    mangen_hash_pool_t *pool
) {
    if (pool == NULL) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);

    const bool must_join =
        pool->started && !pool->joined;

    if (must_join) {
        mangen_hash_pool_abort_locked(pool);
    }

    const size_t count = pool->threads_started;

    pthread_mutex_unlock(&pool->mutex);

    if (must_join) {
        for (size_t i = 0u; i < count; ++i) {
            pthread_join(pool->threads[i], NULL);
        }
    }

    mangen_queue_destroy(pool->queue);

    pthread_cond_destroy(&pool->work_ready);
    pthread_mutex_destroy(&pool->mutex);

    free(pool->threads);
    free(pool);
}
