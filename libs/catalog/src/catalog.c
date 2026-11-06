#include "catalog_internal.h"

#include <stdlib.h>

mangen_catalog_status_t mangen_catalog_create(
    mangen_catalog_t **out
) {
    if (out == NULL) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    *out = NULL;

    mangen_catalog_t *catalog = calloc(
        1,
        sizeof(*catalog)
    );

    if (catalog == NULL) {
        return MANGEN_CATALOG_OUT_OF_MEMORY;
    }

    if (pthread_mutex_init(&catalog->mutex, NULL) != 0) {
        free(catalog);
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    if (pthread_cond_init(&catalog->ready, NULL) != 0) {
        pthread_mutex_destroy(&catalog->mutex);
        free(catalog);

        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    catalog->entries = NULL;
    catalog->count = 0;

    catalog->finalized = false;
    catalog->cancelled = false;

    *out = catalog;

    return MANGEN_CATALOG_OK;
}

void mangen_catalog_destroy(
    mangen_catalog_t *catalog
) {
    if (catalog == NULL) {
        return;
    }

    for (size_t i = 0; i < catalog->count; ++i) {
        free(catalog->entries[i].path);
    }

    free(catalog->entries);

    pthread_cond_destroy(&catalog->ready);
    pthread_mutex_destroy(&catalog->mutex);

    free(catalog);
}

size_t mangen_catalog_size(
    const mangen_catalog_t *catalog
) {
    if (catalog == NULL || !catalog->finalized) {
        return 0;
    }

    return catalog->count;
}

mangen_catalog_entry_t *mangen_catalog_entry_at(
    mangen_catalog_t *catalog,
    size_t index
) {
    if (catalog == NULL || !catalog->finalized) {
        return NULL;
    }

    if (index >= catalog->count) {
        return NULL;
    }

    return &catalog->entries[index];
}

const char *mangen_catalog_entry_path(
    const mangen_catalog_entry_t *entry
) {
    if (entry == NULL) {
        return NULL;
    }

    return entry->path;
}
