#ifndef MANGEN_CATALOG_H
#define MANGEN_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MANGEN_CATALOG_DIGEST_CAPACITY 64u

typedef struct mangen_catalog mangen_catalog_t;
typedef struct mangen_catalog_batch mangen_catalog_batch_t;
typedef struct mangen_catalog_entry mangen_catalog_entry_t;

typedef enum {
    MANGEN_CATALOG_OK = 0,
    MANGEN_CATALOG_INVALID_ARGUMENT,
    MANGEN_CATALOG_OUT_OF_MEMORY,
    MANGEN_CATALOG_LIMIT_EXCEEDED,
    MANGEN_CATALOG_INVALID_STATE,
    MANGEN_CATALOG_CANCELLED,
    MANGEN_CATALOG_SYSTEM_ERROR
} mangen_catalog_status_t;

typedef enum {
    MANGEN_CATALOG_PENDING = 0,
    MANGEN_CATALOG_PROCESSING,
    MANGEN_CATALOG_SUCCESS,
    MANGEN_CATALOG_ERROR
} mangen_catalog_state_t;

typedef struct {
    mangen_catalog_state_t state;

    uint8_t digest[MANGEN_CATALOG_DIGEST_CAPACITY];
    size_t digest_size;

    int error;
} mangen_catalog_result_t;

/*
 * Create an empty catalog.
 *
 * On success, *out receives the catalog.
 * On failure, *out is set to NULL.
 */
mangen_catalog_status_t mangen_catalog_create(
    mangen_catalog_t **out
);

/*
 * Destroy a catalog and all owned paths.
 *
 * All threads using the catalog must have stopped.
 * Accepts NULL.
 */
void mangen_catalog_destroy(
    mangen_catalog_t *catalog
);

/*
 * Create a thread-local path batch.
 *
 * Each batch must be modified by only one thread at a time.
 */
mangen_catalog_status_t mangen_catalog_batch_create(
    mangen_catalog_batch_t **out
);

/*
 * Append a path to a batch.
 *
 * owned_path must be allocated with a free()-compatible
 * allocator and must not be empty.
 *
 * On success, ownership transfers to the batch.
 * On failure, ownership remains with the caller.
 */
mangen_catalog_status_t mangen_catalog_batch_append(
    mangen_catalog_batch_t *batch,
    char *owned_path
);

/*
 * Return the number of paths in a batch.
 *
 * Returns zero for NULL.
 */
size_t mangen_catalog_batch_size(
    const mangen_catalog_batch_t *batch
);

/*
 * Destroy a batch and paths still owned by it.
 *
 * Accepts NULL.
 */
void mangen_catalog_batch_destroy(
    mangen_catalog_batch_t *batch
);

/*
 * Merge batches into the catalog.
 *
 * Requirements:
 * - The catalog must not already be finalized.
 * - All walker threads must have completed their batches.
 * - Batches must not be modified concurrently.
 *
 * On success:
 * - Paths are sorted lexicographically.
 * - Identical path strings are deduplicated.
 * - Ownership of paths transfers to the catalog.
 * - All supplied batches become empty.
 * - Entry addresses remain stable until destruction.
 *
 * On failure, ownership remains with the batches
 * and the catalog remains unfinalized.
 *
 * This operation can be performed only once.
 */
mangen_catalog_status_t mangen_catalog_finalize(
    mangen_catalog_t *catalog,
    mangen_catalog_batch_t *const *batches,
    size_t batch_count
);

/*
 * Return the number of catalog entries.
 *
 * Returns zero for NULL or an unfinalized catalog.
 */
size_t mangen_catalog_size(
    const mangen_catalog_t *catalog
);

/*
 * Return a borrowed entry pointer.
 *
 * Requires a finalized catalog and a valid index.
 * Returns NULL otherwise.
 *
 * The pointer remains valid until catalog destruction.
 */
mangen_catalog_entry_t *mangen_catalog_entry_at(
    mangen_catalog_t *catalog,
    size_t index
);

/*
 * Return the immutable path associated with an entry.
 *
 * The returned pointer belongs to the catalog.
 * Returns NULL for a NULL entry.
 */
const char *mangen_catalog_entry_path(
    const mangen_catalog_entry_t *entry
);

/*
 * Transition PENDING -> PROCESSING.
 *
 * Returns INVALID_STATE for any other initial state.
 */
mangen_catalog_status_t mangen_catalog_mark_processing(
    mangen_catalog_entry_t *entry
);

/*
 * Publish a successful hashing result.
 *
 * Requires PROCESSING state.
 * digest_size must be in the range [1, 64].
 *
 * On success, the state becomes SUCCESS.
 */
mangen_catalog_status_t mangen_catalog_publish_success(
    mangen_catalog_entry_t *entry,
    const uint8_t *digest,
    size_t digest_size
);

/*
 * Publish a failed hashing result.
 *
 * Requires PROCESSING state and nonzero error_code.
 *
 * On success, the state becomes ERROR.
 */
mangen_catalog_status_t mangen_catalog_publish_error(
    mangen_catalog_entry_t *entry,
    int error_code
);

/*
 * Copy the current result into *out.
 *
 * PENDING and PROCESSING are valid snapshot states.
 * The operation does not wait.
 */
mangen_catalog_status_t mangen_catalog_get_result(
    mangen_catalog_entry_t *entry,
    mangen_catalog_result_t *out
);

/*
 * Wait for SUCCESS or ERROR and copy the result.
 *
 * Returns CANCELLED if the catalog is cancelled.
 *
 * SUCCESS and ERROR are distinguished through
 * out->state; both return MANGEN_CATALOG_OK.
 */
mangen_catalog_status_t mangen_catalog_wait_result(
    mangen_catalog_entry_t *entry,
    mangen_catalog_result_t *out
);

/*
 * Cancel the catalog and wake all waiting threads.
 *
 * Can be called concurrently with result publication.
 * Repeated calls are allowed.
 */
void mangen_catalog_cancel(
    mangen_catalog_t *catalog
);

#ifdef __cplusplus
}
#endif

#endif
