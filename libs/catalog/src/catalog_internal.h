#ifndef MANGEN_CATALOG_INTERNAL_H
#define MANGEN_CATALOG_INTERNAL_H

#include "mangen/catalog.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mangen_catalog_batch {
    char **paths;

    size_t count;
    size_t capacity;
};

struct mangen_catalog_entry {
    mangen_catalog_t *owner;

    char *path;

    mangen_catalog_state_t state;

    uint8_t digest[MANGEN_CATALOG_DIGEST_CAPACITY];
    size_t digest_size;

    int error;
};

struct mangen_catalog {
    mangen_catalog_entry_t *entries;

    size_t count;

    pthread_mutex_t mutex;
    pthread_cond_t ready;

    bool finalized;
    bool cancelled;
};

#endif
