#define _POSIX_C_SOURCE 200809L

#include "walker_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static mangen_walker_status_t map_queue_status(
    mangen_queue_status_t status
) {
    switch (status) {
        case MANGEN_QUEUE_OK:
            return MANGEN_WALKER_OK;

        case MANGEN_QUEUE_INVALID_ARGUMENT:
            return MANGEN_WALKER_INVALID_ARGUMENT;

        case MANGEN_QUEUE_OUT_OF_MEMORY:
            return MANGEN_WALKER_OUT_OF_MEMORY;

        case MANGEN_QUEUE_LIMIT_EXCEEDED:
            return MANGEN_WALKER_LIMIT_EXCEEDED;

        default:
            return MANGEN_WALKER_SYSTEM_ERROR;
    }
}

static mangen_walker_status_t map_catalog_status(
    mangen_catalog_status_t status
) {
    switch (status) {
        case MANGEN_CATALOG_OK:
            return MANGEN_WALKER_OK;

        case MANGEN_CATALOG_OUT_OF_MEMORY:
            return MANGEN_WALKER_OUT_OF_MEMORY;

        case MANGEN_CATALOG_LIMIT_EXCEEDED:
            return MANGEN_WALKER_LIMIT_EXCEEDED;

        default:
            return MANGEN_WALKER_SYSTEM_ERROR;
    }
}

static bool valid_options(
    const mangen_walker_options_t *options
) {
    if (options == NULL ||
        options->root_path == NULL ||
        options->root_path[0] == '\0' ||
        options->worker_count == 0u ||
        options->queue_capacity == 0u) {
        return false;
    }

    if (options->exclusion_count > 0u &&
        options->exclusions == NULL) {
        return false;
    }

    for (size_t i = 0u;
         i < options->exclusion_count;
         ++i) {
        if (options->exclusions[i] == NULL) {
            return false;
        }
    }

    return true;
}

static void collect_report(
    const mangen_walker_shared_t *shared,
    mangen_walker_report_t *report
) {
    memset(report, 0, sizeof(*report));

    for (size_t i = 0u;
         i < shared->worker_count;
         ++i) {
        const mangen_walker_report_t *local =
            &shared->workers[i].report;

        report->directories_visited +=
            local->directories_visited;

        report->files_discovered +=
            local->files_discovered;

        report->files_excluded +=
            local->files_excluded;

        report->directories_excluded +=
            local->directories_excluded;

        report->io_errors +=
            local->io_errors;
    }
}

mangen_walker_status_t mangen_walker_scan(
    const mangen_walker_options_t *options,
    mangen_catalog_t *catalog,
    mangen_walker_report_t *report
) {
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
    }

    if (!valid_options(options) || catalog == NULL) {
        return MANGEN_WALKER_INVALID_ARGUMENT;
    }

    if (options->worker_count >
        SIZE_MAX / sizeof(mangen_walker_worker_t)) {
        return MANGEN_WALKER_LIMIT_EXCEEDED;
    }

    char *root_path = realpath(
        options->root_path,
        NULL
    );

    if (root_path == NULL) {
        return errno == ENOMEM
            ? MANGEN_WALKER_OUT_OF_MEMORY
            : MANGEN_WALKER_INVALID_ROOT;
    }

    struct stat root_metadata;

    if (stat(root_path, &root_metadata) != 0 ||
        !S_ISDIR(root_metadata.st_mode)) {
        free(root_path);
        return MANGEN_WALKER_INVALID_ROOT;
    }

    mangen_walker_shared_t shared = {0};

    shared.options = options;
    shared.catalog = catalog;
    shared.worker_count = options->worker_count;
    shared.fatal_status = MANGEN_WALKER_OK;

    mangen_walker_status_t result = MANGEN_WALKER_OK;

    bool mutex_initialized = false;
    bool log_mutex_initialized = false;
    bool condition_initialized = false;

    if (pthread_mutex_init(&shared.mutex, NULL) != 0) {
        result = MANGEN_WALKER_SYSTEM_ERROR;
        goto cleanup;
    }

    mutex_initialized = true;

    if (pthread_mutex_init(&shared.log_mutex, NULL) != 0) {
        result = MANGEN_WALKER_SYSTEM_ERROR;
        goto cleanup;
    }

    log_mutex_initialized = true;

    if (pthread_cond_init(&shared.work_ready, NULL) != 0) {
        result = MANGEN_WALKER_SYSTEM_ERROR;
        goto cleanup;
    }

    condition_initialized = true;

    const mangen_queue_status_t queue_status =
        mangen_queue_create(
            options->queue_capacity,
            mangen_walker_job_dispose,
            NULL,
            &shared.queue
        );

    if (queue_status != MANGEN_QUEUE_OK) {
        result = map_queue_status(queue_status);
        goto cleanup;
    }

    shared.workers = calloc(
        shared.worker_count,
        sizeof(*shared.workers)
    );

    if (shared.workers == NULL) {
        result = MANGEN_WALKER_OUT_OF_MEMORY;
        goto cleanup;
    }

    for (size_t i = 0u;
         i < shared.worker_count;
         ++i) {
        mangen_walker_worker_t *worker =
            &shared.workers[i];

        worker->shared = &shared;

        const mangen_catalog_status_t batch_status =
            mangen_catalog_batch_create(
                &worker->batch
            );

        if (batch_status != MANGEN_CATALOG_OK) {
            result = map_catalog_status(batch_status);
            goto cleanup;
        }
    }

    mangen_walker_dir_job_t *root_job =
        mangen_walker_job_create(
            root_path,
            ""
        );

    if (root_job == NULL) {
        result = MANGEN_WALKER_OUT_OF_MEMORY;
        goto cleanup;
    }

    result = mangen_walker_enqueue_directory(
        &shared,
        root_job
    );

    if (result != MANGEN_WALKER_OK) {
        mangen_walker_job_dispose(root_job, NULL);
        goto cleanup;
    }

    /*
     * The root job is now owned by the queue.
     * pending_jobs == 1.
     */

    for (size_t i = 0u;
         i < shared.worker_count;
         ++i) {
        mangen_walker_worker_t *worker =
            &shared.workers[i];

        const int rc = pthread_create(
            &worker->thread,
            NULL,
            mangen_walker_thread_main,
            worker
        );

        if (rc != 0) {
            result = MANGEN_WALKER_SYSTEM_ERROR;

            mangen_walker_abort(
                &shared,
                result
            );

            break;
        }

        ++shared.threads_started;
    }

    /*
     * All created threads must finish before
     * batches and the queue can be destroyed.
     */
    for (size_t i = 0u;
         i < shared.threads_started;
         ++i) {
        const int rc = pthread_join(
            shared.workers[i].thread,
            NULL
        );

        if (rc != 0) {
            result = MANGEN_WALKER_SYSTEM_ERROR;
        }
    }

    if (result == MANGEN_WALKER_OK && shared.aborted) {
        result = shared.fatal_status;
    }

    mangen_walker_report_t collected = {0};

    collect_report(&shared, &collected);

    if (report != NULL) {
        *report = collected;
    }

    if (result != MANGEN_WALKER_OK) {
        goto cleanup;
    }

    mangen_catalog_batch_t **batches = calloc(
        shared.worker_count,
        sizeof(*batches)
    );

    if (batches == NULL) {
        result = MANGEN_WALKER_OUT_OF_MEMORY;
        goto cleanup;
    }

    for (size_t i = 0u;
         i < shared.worker_count;
         ++i) {
        batches[i] = shared.workers[i].batch;
    }

    const mangen_catalog_status_t finalize_status =
        mangen_catalog_finalize(
            catalog,
            batches,
            shared.worker_count
        );

    free(batches);

    if (finalize_status != MANGEN_CATALOG_OK) {
        result = map_catalog_status(finalize_status);
        goto cleanup;
    }

    if (collected.io_errors > 0u) {
        result = MANGEN_WALKER_PARTIAL_IO_ERROR;
    }

cleanup:
    if (shared.workers != NULL) {
        for (size_t i = 0u;
             i < shared.worker_count;
             ++i) {
            mangen_catalog_batch_destroy(
                shared.workers[i].batch
            );
        }

        free(shared.workers);
    }

    if (shared.queue != NULL) {
        mangen_queue_destroy(shared.queue);
    }

    if (condition_initialized) {
        pthread_cond_destroy(&shared.work_ready);
    }

    if (log_mutex_initialized) {
        pthread_mutex_destroy(&shared.log_mutex);
    }

    if (mutex_initialized) {
        pthread_mutex_destroy(&shared.mutex);
    }

    free(root_path);

    return result;
}
