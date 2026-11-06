#include "catalog_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int compare_paths(
    const void *left,
    const void *right
) {
    char *const *a = left;
    char *const *b = right;

    return strcmp(*a, *b);
}

static mangen_catalog_status_t validate_batches(
    mangen_catalog_batch_t *const *batches,
    size_t batch_count,
    size_t *total_count
) {
    size_t total = 0;

    for (size_t i = 0; i < batch_count; ++i) {
        mangen_catalog_batch_t *batch = batches[i];

        if (batch == NULL) {
            return MANGEN_CATALOG_INVALID_ARGUMENT;
        }

        if (
            batch->count > batch->capacity ||
            (batch->count > 0 && batch->paths == NULL)
        ) {
            return MANGEN_CATALOG_INVALID_STATE;
        }

        for (size_t j = 0; j < i; ++j) {
            if (batch == batches[j]) {
                return MANGEN_CATALOG_INVALID_ARGUMENT;
            }
        }

        if (batch->count > SIZE_MAX - total) {
            return MANGEN_CATALOG_LIMIT_EXCEEDED;
        }

        total += batch->count;
    }

    *total_count = total;

    return MANGEN_CATALOG_OK;
}

static void release_batch_arrays(
    mangen_catalog_batch_t *const *batches,
    size_t batch_count
) {
    for (size_t i = 0; i < batch_count; ++i) {
        mangen_catalog_batch_t *batch = batches[i];

        free(batch->paths);

        batch->paths = NULL;
        batch->count = 0;
        batch->capacity = 0;
    }
}

mangen_catalog_status_t mangen_catalog_finalize(
    mangen_catalog_t *catalog,
    mangen_catalog_batch_t *const *batches,
    size_t batch_count
) {
    if (
        catalog == NULL ||
        (batch_count > 0 && batches == NULL)
    ) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    if (pthread_mutex_lock(&catalog->mutex) != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    if (catalog->cancelled) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_CANCELLED;
    }

    if (catalog->finalized) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_INVALID_STATE;
    }

    size_t total_count = 0;

    mangen_catalog_status_t status = validate_batches(
        batches,
        batch_count,
        &total_count
    );

    if (status != MANGEN_CATALOG_OK) {
        pthread_mutex_unlock(&catalog->mutex);
        return status;
    }

    if (total_count == 0) {
        release_batch_arrays(batches, batch_count);

        catalog->finalized = true;

        pthread_mutex_unlock(&catalog->mutex);

        return MANGEN_CATALOG_OK;
    }

    if (total_count > SIZE_MAX / sizeof(char *)) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_LIMIT_EXCEEDED;
    }

    char **sorted_paths = malloc(
        total_count * sizeof(*sorted_paths)
    );

    if (sorted_paths == NULL) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_OUT_OF_MEMORY;
    }

    size_t position = 0;

    for (size_t i = 0; i < batch_count; ++i) {
        const mangen_catalog_batch_t *batch = batches[i];

        for (size_t j = 0; j < batch->count; ++j) {
            sorted_paths[position++] = batch->paths[j];
        }
    }

    qsort(
        sorted_paths,
        total_count,
        sizeof(*sorted_paths),
        compare_paths
    );

    size_t unique_count = 1;

    for (size_t i = 1; i < total_count; ++i) {
        if (
            strcmp(
                sorted_paths[i - 1],
                sorted_paths[i]
            ) != 0
        ) {
            ++unique_count;
        }
    }

    if (
        unique_count >
        SIZE_MAX / sizeof(mangen_catalog_entry_t)
    ) {
        free(sorted_paths);
        pthread_mutex_unlock(&catalog->mutex);

        return MANGEN_CATALOG_LIMIT_EXCEEDED;
    }

    mangen_catalog_entry_t *entries = calloc(
        unique_count,
        sizeof(*entries)
    );

    if (entries == NULL) {
        free(sorted_paths);
        pthread_mutex_unlock(&catalog->mutex);

        return MANGEN_CATALOG_OUT_OF_MEMORY;
    }

    size_t entry_index = 0;

    for (size_t i = 0; i < total_count; ++i) {
        char *path = sorted_paths[i];

        if (
            entry_index > 0 &&
            strcmp(
                path,
                entries[entry_index - 1].path
            ) == 0
        ) {
            if (path != entries[entry_index - 1].path) {
                free(path);
            }

            continue;
        }

        mangen_catalog_entry_t *entry =
            &entries[entry_index];

        entry->owner = catalog;
        entry->path = path;

        entry->state = MANGEN_CATALOG_PENDING;

        entry->digest_size = 0;
        entry->error = 0;

        ++entry_index;
    }

    release_batch_arrays(batches, batch_count);

    catalog->entries = entries;
    catalog->count = unique_count;
    catalog->finalized = true;

    free(sorted_paths);

    pthread_mutex_unlock(&catalog->mutex);

    return MANGEN_CATALOG_OK;
}
