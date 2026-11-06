#include "catalog_internal.h"

#include <stdint.h>
#include <stdlib.h>

enum {
    MANGEN_CATALOG_INITIAL_CAPACITY = 16
};

mangen_catalog_status_t mangen_catalog_batch_create(
    mangen_catalog_batch_t **out
) {
    if (out == NULL) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    *out = NULL;

    mangen_catalog_batch_t *batch = calloc(
        1,
        sizeof(*batch)
    );

    if (batch == NULL) {
        return MANGEN_CATALOG_OUT_OF_MEMORY;
    }

    *out = batch;

    return MANGEN_CATALOG_OK;
}

static mangen_catalog_status_t batch_reserve(
    mangen_catalog_batch_t *batch
) {
    const size_t max_capacity =
        SIZE_MAX / sizeof(*batch->paths);

    if (batch->count < batch->capacity) {
        return MANGEN_CATALOG_OK;
    }

    if (batch->capacity >= max_capacity) {
        return MANGEN_CATALOG_LIMIT_EXCEEDED;
    }

    size_t new_capacity;

    if (batch->capacity == 0) {
        new_capacity = MANGEN_CATALOG_INITIAL_CAPACITY;
    } else if (batch->capacity > max_capacity / 2) {
        new_capacity = max_capacity;
    } else {
        new_capacity = batch->capacity * 2;
    }

    if (new_capacity > max_capacity) {
        new_capacity = max_capacity;
    }

    if (new_capacity <= batch->capacity) {
        return MANGEN_CATALOG_LIMIT_EXCEEDED;
    }

    char **new_paths = realloc(
        batch->paths,
        new_capacity * sizeof(*batch->paths)
    );

    if (new_paths == NULL) {
        return MANGEN_CATALOG_OUT_OF_MEMORY;
    }

    batch->paths = new_paths;
    batch->capacity = new_capacity;

    return MANGEN_CATALOG_OK;
}

mangen_catalog_status_t mangen_catalog_batch_append(
    mangen_catalog_batch_t *batch,
    char *owned_path
) {
    if (
        batch == NULL ||
        owned_path == NULL ||
        owned_path[0] == '\0'
    ) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    mangen_catalog_status_t status = batch_reserve(
        batch
    );

    if (status != MANGEN_CATALOG_OK) {
        return status;
    }

    batch->paths[batch->count] = owned_path;
    ++batch->count;

    return MANGEN_CATALOG_OK;
}

size_t mangen_catalog_batch_size(
    const mangen_catalog_batch_t *batch
) {
    return batch != NULL ? batch->count : 0;
}

void mangen_catalog_batch_destroy(
    mangen_catalog_batch_t *batch
) {
    if (batch == NULL) {
        return;
    }

    for (size_t i = 0; i < batch->count; ++i) {
        free(batch->paths[i]);
    }

    free(batch->paths);
    free(batch);
}
