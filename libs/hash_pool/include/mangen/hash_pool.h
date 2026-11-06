#ifndef MANGEN_HASH_POOL_H
#define MANGEN_HASH_POOL_H

#include <stddef.h>

#include "mangen/catalog.h"
#include "mangen/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mangen_hash_pool mangen_hash_pool_t;

typedef enum {
    MANGEN_HASH_POOL_OK = 0,
    MANGEN_HASH_POOL_INVALID_ARGUMENT,
    MANGEN_HASH_POOL_INVALID_STATE,
    MANGEN_HASH_POOL_OUT_OF_MEMORY,
    MANGEN_HASH_POOL_LIMIT_EXCEEDED,
    MANGEN_HASH_POOL_QUEUE_FULL,
    MANGEN_HASH_POOL_CANCELLED,
    MANGEN_HASH_POOL_SYSTEM_ERROR
} mangen_hash_pool_status_t;

mangen_hash_pool_status_t mangen_hash_pool_create(
    mangen_catalog_t *catalog,
    mangen_hash_algorithm_t algorithm,
    size_t worker_count,
    size_t queue_capacity,
    mangen_hash_pool_t **out
);

mangen_hash_pool_status_t mangen_hash_pool_start(
    mangen_hash_pool_t *pool
);

mangen_hash_pool_status_t mangen_hash_pool_submit(
    mangen_hash_pool_t *pool,
    size_t catalog_index
);

mangen_hash_pool_status_t mangen_hash_pool_close(
    mangen_hash_pool_t *pool
);

mangen_hash_pool_status_t mangen_hash_pool_join(
    mangen_hash_pool_t *pool
);

void mangen_hash_pool_cancel(
    mangen_hash_pool_t *pool
);

size_t mangen_hash_pool_completed(
    mangen_hash_pool_t *pool
);

size_t mangen_hash_pool_failed(
    mangen_hash_pool_t *pool
);

void mangen_hash_pool_destroy(
    mangen_hash_pool_t *pool
);

#ifdef __cplusplus
}
#endif

#endif
