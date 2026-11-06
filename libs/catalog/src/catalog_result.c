#include "catalog_internal.h"

#include <string.h>

mangen_catalog_status_t mangen_catalog_mark_processing(
    mangen_catalog_entry_t *entry
) {
    if (entry == NULL || entry->owner == NULL) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    mangen_catalog_t *catalog = entry->owner;

    if (pthread_mutex_lock(&catalog->mutex) != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    mangen_catalog_status_t status;

    if (catalog->cancelled) {
        status = MANGEN_CATALOG_CANCELLED;
    } else if (entry->state != MANGEN_CATALOG_PENDING) {
        status = MANGEN_CATALOG_INVALID_STATE;
    } else {
        entry->state = MANGEN_CATALOG_PROCESSING;
        status = MANGEN_CATALOG_OK;
    }

    if (pthread_mutex_unlock(&catalog->mutex) != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    return status;
}

mangen_catalog_status_t mangen_catalog_publish_success(
    mangen_catalog_entry_t *entry,
    const uint8_t *digest,
    size_t digest_size
) {
    if (
        entry == NULL ||
        entry->owner == NULL ||
        digest == NULL ||
        digest_size == 0 ||
        digest_size > MANGEN_CATALOG_DIGEST_CAPACITY
    ) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    mangen_catalog_t *catalog = entry->owner;

    if (pthread_mutex_lock(&catalog->mutex) != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    if (catalog->cancelled) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_CANCELLED;
    }

    if (entry->state != MANGEN_CATALOG_PROCESSING) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_INVALID_STATE;
    }

    memset(
        entry->digest,
        0,
        sizeof(entry->digest)
    );

    memcpy(
        entry->digest,
        digest,
        digest_size
    );

    entry->digest_size = digest_size;
    entry->error = 0;

    entry->state = MANGEN_CATALOG_SUCCESS;

    const int signal_result = pthread_cond_broadcast(
        &catalog->ready
    );

    const int unlock_result = pthread_mutex_unlock(
        &catalog->mutex
    );

    if (signal_result != 0 || unlock_result != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    return MANGEN_CATALOG_OK;
}

mangen_catalog_status_t mangen_catalog_publish_error(
    mangen_catalog_entry_t *entry,
    int error_code
) {
    if (
        entry == NULL ||
        entry->owner == NULL ||
        error_code == 0
    ) {
        return MANGEN_CATALOG_INVALID_ARGUMENT;
    }

    mangen_catalog_t *catalog = entry->owner;

    if (pthread_mutex_lock(&catalog->mutex) != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    if (catalog->cancelled) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_CANCELLED;
    }

    if (entry->state != MANGEN_CATALOG_PROCESSING) {
        pthread_mutex_unlock(&catalog->mutex);
        return MANGEN_CATALOG_INVALID_STATE;
    }

    memset(
        entry->digest,
        0,
        sizeof(entry->digest)
    );

    entry->digest_size = 0;
    entry->error = error_code;

    entry->state = MANGEN_CATALOG_ERROR;

    const int signal_result = pthread_cond_broadcast(
        &catalog->ready
    );

    const int unlock_result = pthread_mutex_unlock(
        &catalog->mutex
    );

    if (signal_result != 0 || unlock_result != 0) {
        return MANGEN_CATALOG_SYSTEM_ERROR;
    }

    return MANGEN_CATALOG_OK;
}

mangen_catalog_status_t mangen_catalog_get_result(
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
