#include "hash_pool_internal.h"

#include <errno.h>
#include <stdint.h>

static void abort_pool(mangen_hash_pool_t *pool) {
    pthread_mutex_lock(&pool->mutex);

    mangen_hash_pool_abort_locked(pool);

    pthread_mutex_unlock(&pool->mutex);
}

static bool process_entry(
    mangen_hash_pool_t *pool,
    mangen_catalog_entry_t *entry
) {
    const char *path = mangen_catalog_entry_path(entry);

    mangen_hash_digest_t digest = {0};
    int detail = 0;

    mangen_hash_status_t hash_status;

    if (path == NULL) {
        hash_status = MANGEN_HASH_INVALID_ARGUMENT;
        detail = EINVAL;
    } else {
        hash_status = mangen_hash_file(
            path,
            pool->algorithm,
            &digest,
            &detail
        );
    }

    mangen_catalog_status_t catalog_status;

    if (hash_status == MANGEN_HASH_OK) {
        catalog_status = mangen_catalog_publish_success(
            entry,
            digest.bytes,
            digest.size
        );
    } else {
        /*
         * Catalog stores a POSIX-style error code.
         * Mbed TLS internal errors are represented as EIO.
         */
        const int error_code =
            detail > 0 ? detail : EIO;

        catalog_status = mangen_catalog_publish_error(
            entry,
            error_code
        );
    }

    if (catalog_status == MANGEN_CATALOG_CANCELLED) {
        return false;
    }

    if (catalog_status != MANGEN_CATALOG_OK) {
        abort_pool(pool);
        return false;
    }

    pthread_mutex_lock(&pool->mutex);

    ++pool->completed_count;

    if (hash_status != MANGEN_HASH_OK) {
        ++pool->failed_count;
    }

    pthread_mutex_unlock(&pool->mutex);

    return true;
}

void *mangen_hash_pool_worker(void *argument) {
    mangen_hash_pool_t *pool = argument;

    if (pool == NULL) {
        return NULL;
    }

    for (;;) {
        pthread_mutex_lock(&pool->mutex);

        void *item = NULL;

        for (;;) {
            if (pool->cancelled) {
                pthread_mutex_unlock(&pool->mutex);
                return NULL;
            }

            const mangen_queue_status_t status =
                mangen_queue_try_pop(
                    pool->queue,
                    &item
                );

            if (status == MANGEN_QUEUE_OK) {
                break;
            }

            if (status == MANGEN_QUEUE_CLOSED) {
                pthread_mutex_unlock(&pool->mutex);
                return NULL;
            }

            if (status == MANGEN_QUEUE_ABORTED) {
                mangen_hash_pool_abort_locked(pool);

                pthread_mutex_unlock(&pool->mutex);
                return NULL;
            }

            if (status != MANGEN_QUEUE_EMPTY) {
                mangen_hash_pool_abort_locked(pool);

                pthread_mutex_unlock(&pool->mutex);
                return NULL;
            }

            /*
             * Queue is open but currently empty.
             * Wait without consuming CPU.
             */
            const int rc = pthread_cond_wait(
                &pool->work_ready,
                &pool->mutex
            );

            if (rc != 0) {
                mangen_hash_pool_abort_locked(pool);

                pthread_mutex_unlock(&pool->mutex);
                return NULL;
            }
        }

        /*
         * Work is performed without holding pool->mutex.
         * Other workers can process entries concurrently.
         */
        pthread_mutex_unlock(&pool->mutex);

        mangen_catalog_entry_t *entry = item;

        if (entry == NULL) {
            abort_pool(pool);
            return NULL;
        }

        if (!process_entry(pool, entry)) {
            return NULL;
        }
    }
}
