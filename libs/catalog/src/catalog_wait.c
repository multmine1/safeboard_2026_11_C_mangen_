#include "catalog_internal.h"

#include <string.h>

mangen_catalog_status_t mangen_catalog_wait_result(
    mangen_catalog_entry_t *entry,
    mangen_catalog_result_t *out
) {
    if (
        entry == NULL ||
        entry->owner == NULL ||
        out == NULL
    ) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    mangen_catalog_t *catalog = entry->owner;

    if (pthread_mutex_lock(&catalog->mutex) != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    while (
        !catalog->cancelled &&
        entry->state != MANGEN_CATALOG_SUCCESS &&
        entry->state != MANGEN_CATALOG_ERROR
    ) {
        if (
            pthread_cond_wait(
                &catalog->ready,
                &catalog->mutex
            ) != 0
        ) {
            pthread_mutex_unlock(&catalog->mutex);

            return MANGEN_CATALOG_SYSTEM_ERROR;
        }
    }

    if (catalog->cancelled) {
        pthread_mutex_unlock(&catalog->mutex);

        return MANGEN_CATALOG_CANCELLED;
    }

    out->state = entry->state;

    memcpy(
        out->digest,
        entry->digest,
        sizeof(out->digest)
    );

    out->digest_size = entry->digest_size;
    out->error = entry->error;

    if (pthread_mutex_unlock(&catalog->mutex) != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    return MANGEN_CATALOG_OK;
}

void mangen_catalog_cancel(
    mangen_catalog_t *catalog
) {
    if (catalog == NULL) {
        return;
    }

    if (pthread_mutex_lock(&catalog->mutex) != 0) {
        return;
    }

    catalog->cancelled = true;

    pthread_cond_broadcast(&catalog->ready);

    pthread_mutex_unlock(&catalog->mutex);
}
